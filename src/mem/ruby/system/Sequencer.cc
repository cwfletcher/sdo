/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/ruby/system/Sequencer.hh"

#include "arch/x86/ldstflags.hh"
#include "base/logging.hh"
#include "base/str.hh"
#include "cpu/testers/rubytest/RubyTester.hh"
#include "debug/MemoryAccess.hh"
#include "debug/ProtocolTrace.hh"
#include "debug/RubySequencer.hh"
#include "debug/RubyStats.hh"
#include "debug/SpecBuffer.hh"
#include "debug/SpecBufferValidate.hh"
#include "debug/JY_Ruby.hh"
#include "mem/packet.hh"
#include "mem/protocol/PrefetchBit.hh"
#include "mem/protocol/RubyAccessMode.hh"
#include "mem/ruby/profiler/Profiler.hh"
#include "mem/ruby/slicc_interface/RubyRequest.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "sim/system.hh"

using namespace std;

Sequencer *
RubySequencerParams::create()
{
    return new Sequencer(this);
}

Sequencer::Sequencer(const Params *p)
    : RubyPort(p), m_IncompleteTimes(MachineType_NUM),
      deadlockCheckEvent([this]{ wakeup(); }, "Sequencer deadlock check"),
      m_specBuf(1024),  // Jiyong: specbuffer for storing value load by obls
      specBufHitEvent([this]{ specBufHitCallback(); }, "Sequencer spec buffer hit") // Jiyong: remove SpecBuffer related code
{
    m_outstanding_count = 0;

    m_instCache_ptr = p->icache;
    m_dataCache_ptr = p->dcache;
    m_data_cache_hit_latency = p->dcache_hit_latency;
    m_inst_cache_hit_latency = p->icache_hit_latency;
    m_max_outstanding_requests = p->max_outstanding_requests;
    m_deadlock_threshold = p->deadlock_threshold;

    m_coreId = p->coreid; // for tracking the two CorePair sequencers
    assert(m_max_outstanding_requests > 0);
    assert(m_deadlock_threshold > 0);
    assert(m_instCache_ptr != NULL);
    assert(m_dataCache_ptr != NULL);
    assert(m_data_cache_hit_latency > 0);
    assert(m_inst_cache_hit_latency > 0);

    m_runningGarnetStandalone = p->garnet_standalone;

    printf("Sequencer of core %d has max_outstanding_requests=%d\n", m_coreId, m_max_outstanding_requests);
}

Sequencer::~Sequencer()
{
}

void
Sequencer::wakeup()
{
    assert(drainState() != DrainState::Draining);

    // Check for deadlock of any of the requests
    Cycles current_time = curCycle();

    // Check across all outstanding requests
    int total_outstanding = 0;

    RequestTable::iterator read = m_readRequestTable.begin();
    RequestTable::iterator read_end = m_readRequestTable.end();
    for (; read != read_end; ++read) {
        SequencerRequest* request = read->second;
        DPRINTFR(JY_Ruby, "Stuck packet: sn:%lli, isSpec=%d\n", request->pkt->seqNum, request->pkt->isSpec());
        if (current_time - request->issue_time < m_deadlock_threshold)
            continue;

        panic("Possible Deadlock detected. Aborting!\n"
              "version: %d request.paddr: 0x%x m_readRequestTable: %d "
              "current time: %u issue_time: %d difference: %d\n", m_version,
              request->pkt->getAddr(), m_readRequestTable.size(),
              current_time * clockPeriod(), request->issue_time * clockPeriod(),
              (current_time * clockPeriod()) - (request->issue_time * clockPeriod()));
    }

    RequestTable::iterator write = m_writeRequestTable.begin();
    RequestTable::iterator write_end = m_writeRequestTable.end();
    for (; write != write_end; ++write) {
        SequencerRequest* request = write->second;
        if (current_time - request->issue_time < m_deadlock_threshold)
            continue;

        panic("Possible Deadlock detected. Aborting!\n"
              "version: %d request.paddr: 0x%x m_writeRequestTable: %d "
              "current time: %u issue_time: %d difference: %d\n", m_version,
              request->pkt->getAddr(), m_writeRequestTable.size(),
              current_time * clockPeriod(), request->issue_time * clockPeriod(),
              (current_time * clockPeriod()) - (request->issue_time * clockPeriod()));
    }

    SpecRequestTable::iterator specld = m_specldRequestTable.begin();
    SpecRequestTable::iterator specld_end = m_specldRequestTable.end();
    for (; specld != specld_end; ++specld) {
        SequencerRequest* request = specld->second;
        DPRINTFR(JY_Ruby, "Stuck packet: sn:%lli, isSpec=%d\n", request->pkt->seqNum, request->pkt->isSpec());
        if (current_time - request->issue_time < m_deadlock_threshold)
            continue;

        panic("Possible Deadlock detected. Aborting!\n"
              "version: %d request.paddr: 0x%x m_specreadRequestTable: %d "
              "current time: %u issue_time: %d difference: %d\n", m_version,
              request->pkt->getAddr(), m_specldRequestTable.size(),
              current_time * clockPeriod(), request->issue_time * clockPeriod(),
              (current_time * clockPeriod()) - (request->issue_time * clockPeriod()));
    }

    total_outstanding += m_writeRequestTable.size();
    total_outstanding += m_readRequestTable.size();
    total_outstanding += m_specldRequestTable.size();

    assert(m_outstanding_count == total_outstanding);

    if (m_outstanding_count > 0) {
        // If there are still outstanding requests, keep checking
        schedule(deadlockCheckEvent, clockEdge(m_deadlock_threshold));
    }
}

void Sequencer::resetStats()
{
    m_latencyHist.reset();
    m_hitLatencyHist.reset();
    m_missLatencyHist.reset();
    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_typeLatencyHist[i]->reset();
        m_hitTypeLatencyHist[i]->reset();
        m_missTypeLatencyHist[i]->reset();
        for (int j = 0; j < MachineType_NUM; j++) {
            m_hitTypeMachLatencyHist[i][j]->reset();
            m_missTypeMachLatencyHist[i][j]->reset();
        }
    }

    for (int i = 0; i < MachineType_NUM; i++) {
        m_missMachLatencyHist[i]->reset();
        m_hitMachLatencyHist[i]->reset();

        m_IssueToInitialDelayHist[i]->reset();
        m_InitialToForwardDelayHist[i]->reset();
        m_ForwardToFirstResponseDelayHist[i]->reset();
        m_FirstResponseToCompletionDelayHist[i]->reset();

        m_IncompleteTimes[i] = 0;
    }
}

// [SafeSpec] Request on the way from CPU to Ruby
// Insert the request on the correct request table.  Return true if
// the entry was already present.
RequestStatus
Sequencer::insertRequest(PacketPtr pkt, RubyRequestType request_type)
{
    DPRINTF(JY_Ruby, "insertRequest\n");
    assert(m_outstanding_count ==
        (m_writeRequestTable.size() + m_readRequestTable.size()) + m_specldRequestTable.size());

    // See if we should schedule a deadlock check
    if (!deadlockCheckEvent.scheduled() &&
        drainState() != DrainState::Draining) {
        schedule(deadlockCheckEvent, clockEdge(m_deadlock_threshold));
    }

    Addr line_addr = makeLineAddress(pkt->getAddr());

    // Check if the line is blocked for a Locked_RMW
    if (m_controller->isBlocked(line_addr) &&
        (request_type != RubyRequestType_Locked_RMW_Write)) {
        // Return that this request's cache line address aliases with
        // a prior request that locked the cache line. The request cannot
        // proceed until the cache line is unlocked by a Locked_RMW_Write
        return RequestStatus_Aliased;
    }

    // Create a default entry, mapping the address to NULL, the cast is
    // there to make gcc 4.4 happy
    RequestTable::value_type default_entry(line_addr,
                                           (SequencerRequest*) NULL);

    // [SafeSpec] If store
    if ((request_type == RubyRequestType_ST) ||
        (request_type == RubyRequestType_RMW_Read) ||
        (request_type == RubyRequestType_RMW_Write) ||
        (request_type == RubyRequestType_Load_Linked) ||
        (request_type == RubyRequestType_Store_Conditional) ||
        (request_type == RubyRequestType_Locked_RMW_Read) ||
        (request_type == RubyRequestType_Locked_RMW_Write) ||
        (request_type == RubyRequestType_FLUSH)) {

        // Check if there is any outstanding read request for the same
        // cache line.
        if (m_readRequestTable.count(line_addr) > 0) {
            m_store_waiting_on_load++;
            return RequestStatus_Aliased;
        }

        pair<RequestTable::iterator, bool> r =
            m_writeRequestTable.insert(default_entry);
        if (r.second) {
            RequestTable::iterator i = r.first;
            i->second = new SequencerRequest(pkt, request_type, curCycle());
            m_outstanding_count++;
        } else {
          // There is an outstanding write request for the cache line
          m_store_waiting_on_store++;
          return RequestStatus_Aliased;
        }
    // [SafeSpec] If load
    } else {
        // Check if there is any outstanding write request for the same
        // cache line.
        if (m_writeRequestTable.count(line_addr) > 0) {
            auto M5_VAR_USED i = m_writeRequestTable.find(line_addr);
            DPRINTFR(JY_Ruby, "%10s An outstanding write (sn=%lli) has aliasing\n", curTick(), i->second->pkt->seqNum);
            m_load_waiting_on_store++;
            return RequestStatus_Aliased;
        }

        pair<RequestTable::iterator, bool> r =
            m_readRequestTable.insert(default_entry);

        if (r.second) {
            RequestTable::iterator i = r.first;
            i->second = new SequencerRequest(pkt, request_type, curCycle());
            m_outstanding_count++;
        } else if  (pkt->isExpose()) {
            assert (RubySystem::getMLDOMEnabled());
            // Jiyong, MLDOM: we merge exposure with aliased ongoing request (safe)
            auto i = m_readRequestTable.find(line_addr);
            if (i->second->m_type == RubyRequestType_LD) {
                DPRINTFR(JY_Ruby, "%10s Merging exposure (sn=%lli, idx=%d-%d, addr=%#x) with %d (ongoing aliased, sn=%lli)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), i->second->pkt->reqIdx, m_readRequestTable[line_addr]->pkt->seqNum );
                i->second->dependentRequests.push_back(pkt);
                return RequestStatus_Merged;
            } else {
                DPRINTF(JY_Ruby, "exposure pkt [sn=%lli] aliased with line_addr: 0x%lx, pkt->seqnum=%lli\n", pkt->seqNum, line_addr, m_readRequestTable[line_addr]->pkt->seqNum);
                m_load_waiting_on_load++;
                return RequestStatus_Aliased;
            }
        } else if (pkt->isValidate()) {
            assert (RubySystem::getMLDOMEnabled());
            // Jiyong, MLDOM: we merge validate with aliased ongoing request (safe)
            auto i = m_readRequestTable.find(line_addr);
            if (i->second->m_type == RubyRequestType_LD) {
                DPRINTFR(JY_Ruby, "%10s Merging validation (sn=%lli, idx=%d-%d, addr=%#x) with %d (ongoing aliased, sn=%lli)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), i->second->pkt->reqIdx, m_readRequestTable[line_addr]->pkt->seqNum );
                i->second->dependentRequests.push_back(pkt);
                return RequestStatus_Merged;
            } else {
                DPRINTF(JY_Ruby, "validation pkt [sn=%lli] aliased with line_addr: 0x%lx, pkt->seqnum=%lli\n", pkt->seqNum, line_addr, m_readRequestTable[line_addr]->pkt->seqNum);
                m_load_waiting_on_load++;
                return RequestStatus_Aliased;
            }
        } else {
            // There is an outstanding read request for the cache line
            DPRINTF(JY_Ruby, "reg_ld pkt [sn=%lli] aliased with line_addr: 0x%lx, pkt->seqnum=%lli\n", pkt->seqNum, line_addr, m_readRequestTable[line_addr]->pkt->seqNum);
            m_load_waiting_on_load++;
            return RequestStatus_Aliased;
        }
        DPRINTFR(JY_Ruby, "%10s insert a request to Sequencer RequestTable with (idx=%d-%d, addr=%#x, sn=%lli)\n", curTick(), pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), pkt->seqNum);

        if (!pkt->isExpose() && !pkt->isValidate()) {
            if (RubySystem::getOblSContentionEnabled()) {
                // obls can block normal load in this mode
                for (auto elem: m_specldRequestTable) {
                    auto specld_key = elem.first;
                    SequencerRequest* M5_VAR_USED specld_seq_req = elem.second;
                    if (line_addr == specld_key.first) {
                        DPRINTF(JY_Ruby, "%10s regld sn=%lli alias with inflight load: sn=%lli. Request blocked [oblsContentionEnable]\n", 
                                curTick(), pkt->seqNum, specld_seq_req->pkt->seqNum);
                        //return RequestStatus_Aliased;
                        break;
                    }
                }
            }
            else {
                // obls doesn't interfere normal load
                for (auto elem: m_specldRequestTable) {
                    auto specld_key = elem.first;
                    if (line_addr == specld_key.first) {
                        DPRINTF(JY_Ruby, "%10s regld sn=%lli alias with inflight load: sn=%lli\n", 
                                curTick(), pkt->seqNum, elem.second->pkt->seqNum);
                        break;
                    }
                }
            }
        }
    }

    m_outstandReqHist.sample(m_outstanding_count);
    assert(m_outstanding_count ==
        (m_writeRequestTable.size() + m_readRequestTable.size() + m_specldRequestTable.size()));

    return RequestStatus_Ready;
}

// Jiyong, MLDOM
// insert a spec load request (oblS) to dedicated requestTable
RequestStatus
Sequencer::insertSpecldRequest(PacketPtr pkt, RubyRequestType request_type)
{
    assert(RubySystem::getMLDOMEnabled());

    DPRINTF(JY_Ruby, "insertSpecldRequest\n");
    assert(m_outstanding_count ==
        (m_writeRequestTable.size() + m_readRequestTable.size() + m_specldRequestTable.size()));

    // See if we should schedule a deadlock check
    if (!deadlockCheckEvent.scheduled() &&
        drainState() != DrainState::Draining) {
        schedule(deadlockCheckEvent, clockEdge(m_deadlock_threshold));
    }

    Addr line_addr = makeLineAddress(pkt->getAddr());
    int ld_idx;
    if (pkt->reqIdx == -1)
        ld_idx = pkt->reqIdx;
    else
        ld_idx = pkt->reqIdx * 2 + ( pkt->isFirst() ? 0 : 1);

    // Check if the line is blocked for a Locked_RMW
    if (m_controller->isBlocked(line_addr) &&
        (request_type != RubyRequestType_Locked_RMW_Write)) {
        // Return that this request's cache line address aliases with
        // a prior request that locked the cache line. The request cannot
        // proceed until the cache line is unlocked by a Locked_RMW_Write
        return RequestStatus_Aliased;
    }

    // Create a default entry, mapping the address to NULL, the cast is
    // there to make gcc 4.4 happy
    //SpecldKeyType key(line_addr, ld_idx);
    //SpecRequestTable::value_type specld_entry(key, (SequencerRequest*) NULL);

    // [SafeSpec] If store
    assert (request_type == RubyRequestType_SPEC_LD_L0 ||
            request_type == RubyRequestType_SPEC_LD_L1 ||
            request_type == RubyRequestType_SPEC_LD_L2 ||
            request_type == RubyRequestType_SPEC_LD_Mem ||
            request_type == RubyRequestType_SPEC_LD_Perfect ||
            request_type == RubyRequestType_SPEC_LD_PerfectUnsafe);

    // Jiyong: we don't block the spec load
    // Check if there is any outstanding write request for the same
    // cache line.
    //if (m_writeRequestTable.count(line_addr) > 0) {
        //m_load_waiting_on_store++;  // Jiyong: it's just a stats counting, so count specld as a normal load
        //return RequestStatus_Aliased;
    //}
    if (RubySystem::getOblSContentionEnabled()) {
        // Jiyong: [Unsafe] contention between obls enabled
        // merge if an inflight load has the same line address
        for (auto elem: m_specldRequestTable) {
            auto specld_key = elem.first;
            SequencerRequest* M5_VAR_USED specld_seq_req = elem.second;
            if (line_addr == specld_key.first) {
                DPRINTF(JY_Ruby, "%10s specld sn=%lli alias with inflight load: sn=%lli. Request blocked!!(oblsContentionEnabled)\n", 
                        curTick(), pkt->seqNum, specld_seq_req->pkt->seqNum);
                return RequestStatus_Aliased;
                //DPRINTF(JY_Ruby, "%10s specld sn=%lli alias with inflight load: sn=%lli. Request merged!!(oblsContentionEnabled)\n", 
                        //curTick(), pkt->seqNum, specld_seq_req->pkt->seqNum);
                //specld_seq_req->dependentRequests.push_back(pkt);
                //return RequestStatus_Merged;
            }
        }
    }
    else {
        // Jiyong: default, safe version: oblLds don't contend with each other
        // Two obls with identical line addr can coexist
        for (auto elem: m_specldRequestTable) {
            auto specld_key = elem.first;
            SequencerRequest* M5_VAR_USED specld_seq_req = elem.second;
            if (line_addr == specld_key.first) {
                DPRINTF(JY_Ruby, "%10s specld sn=%lli alias with inflight load: sn=%lli\n", 
                        curTick(), pkt->seqNum, specld_seq_req->pkt->seqNum);
                break;
            }
        }
    }

    SpecRequestTable::iterator i = m_specldRequestTable.find(std::make_pair(line_addr, ld_idx));
    if (i != m_specldRequestTable.end()) {
        DPRINTFR(JY_Ruby, "%10s fail to insert a request to Sequencer SpecLDRequestTable with (addr=%#x, ld_idx=%d, sn=%lli)\n", curTick(), line_addr, ld_idx, pkt->seqNum);
        return RequestStatus_Aliased;
    }

    // Jiyong, SDO: merge specld requests (for loads predictedby loop(streaming) predictor)
    if (pkt->aliased_reqIdx > 0) {
        assert (request_type == RubyRequestType_SPEC_LD_L0); // must be L0 to merge
        DPRINTF(JY_Ruby, "%10s the request [sn=%lli] wishes to merge with load idx=%d\n", curTick(), pkt->seqNum, pkt->aliased_reqIdx);
        int aliased_ld_idx = pkt->aliased_reqIdx * 2;
        // this pkt will be speculatively merged with an other request
        for (auto elem: m_specldRequestTable) {
            auto specld_key = elem.first;
            auto specld_req = elem.second;
            if (specld_key.second == aliased_ld_idx) {
                DPRINTF(JY_Ruby, "%10s the request [sn=%lli] is alised with an older, inflight load [sn=%lli].\n", curTick(), pkt->seqNum, specld_req->pkt->seqNum);
                specld_req->dependentRequests.push_back(pkt);
                DPRINTF(JY_Ruby, "%10s the request [sn=%lli] has a dependent pkt [sn=%lli]\n", curTick(), specld_req->pkt->seqNum, pkt->seqNum);
            }
            return RequestStatus_Merged;
        }
        DPRINTF(JY_Ruby, "%10s the request [sn=%lli] doesn't find inflight load idx=%d\n", curTick(), pkt->seqNum, pkt->aliased_reqIdx);
    }
    //if (pkt->notTakenByLoop) {
        //assert (request_type == RubyRequestType_SPEC_LD_L0); // must be L0 to merge
        //Addr pc = pkt->req->getPC();
        //// this pkt will be speculatively merged with an other request
        //for (auto elem: m_specldRequestTable) {
            //auto specld_req = elem.second;
            //Addr specld_pc = specld_req->pkt->req->getPC();
            //if (specld_req->pkt->takenByLoop && specld_pc == pc) {
                //DPRINTF(JY_Ruby, "%10s the request [sn=%lli] is alised with an older, inflight spec load [sn=%lli], pc = %#x.\n",
                        //curTick(), pkt->seqNum, specld_req->pkt->seqNum, pc);
                //specld_req->dependentRequests.push_back(pkt);
                //DPRINTF(JY_Ruby, "%10s the request [sn=%lli] has a dependent pkt [sn=%lli]\n",
                        //curTick(), specld_req->pkt->seqNum, pkt->seqNum);
            //}
            //return RequestStatus_Merged;
        //}
        //for (auto elem: m_readRequestTable) {
            //auto nonspecld_req = elem.second;
            //Addr nonspecld_pc = nonspecld_req->pkt->req->getPC();
            //if (nonspecld_pc == pc) {
                //DPRINTF(JY_Ruby, "%10s the request [sn=%lli] is alised with an older, inflight nonspec load [sn=%lli], pc = %#x.\n",
                        //curTick(), pkt->seqNum, nonspecld_req->pkt->seqNum, pc);
                //nonspecld_req->dependentRequests.push_back(pkt);
                //DPRINTF(JY_Ruby, "%10s the request [sn=%lli] has a dependent pkt [sn=%lli]\n",
                        //curTick(), nonspecld_req->pkt->seqNum, pkt->seqNum);
            //}
            //return RequestStatus_Merged;
        //}
        //DPRINTF(JY_Ruby, "%10s the request [sn=%lli] doesn't find inflight load idx=%d\n", curTick(), pkt->seqNum, pkt->aliased_reqIdx);
    //}

    DPRINTFR(JY_Ruby, "%10s successfully insert a request to Sequencer SpecLDRequestTable with (addr=%#x, ld_idx=%d, sn=%lli)\n", curTick(), line_addr, ld_idx, pkt->seqNum);
    SequencerRequest* new_seq_req = new SequencerRequest(pkt, request_type, curCycle());
    m_specldRequestTable.insert(std::make_pair(std::make_pair(line_addr, ld_idx), new_seq_req));
    m_outstanding_count++;

    m_outstandReqHist.sample(m_outstanding_count);
    assert(m_outstanding_count ==
        (m_writeRequestTable.size() + m_readRequestTable.size() + m_specldRequestTable.size()));

    return RequestStatus_Ready;
}

void
Sequencer::markRemoved()
{
    m_outstanding_count--;
    assert(m_outstanding_count ==
           m_writeRequestTable.size() + m_readRequestTable.size() + m_specldRequestTable.size());
}

void
Sequencer::invalidateSC(Addr address)
{
    AbstractCacheEntry *e = m_dataCache_ptr->lookup(address);
    // The controller has lost the coherence permissions, hence the lock
    // on the cache line maintained by the cache should be cleared.
    if (e && e->isLocked(m_version)) {
        e->clearLocked();
    }
}

bool
Sequencer::handleLlsc(Addr address, SequencerRequest* request)
{
    AbstractCacheEntry *e = m_dataCache_ptr->lookup(address);
    if (!e)
        return true;

    // The success flag indicates whether the LLSC operation was successful.
    // LL ops will always succeed, but SC may fail if the cache line is no
    // longer locked.
    bool success = true;
    if (request->m_type == RubyRequestType_Store_Conditional) {
        if (!e->isLocked(m_version)) {
            //
            // For failed SC requests, indicate the failure to the cpu by
            // setting the extra data to zero.
            //
            request->pkt->req->setExtraData(0);
            success = false;
        } else {
            //
            // For successful SC requests, indicate the success to the cpu by
            // setting the extra data to one.
            //
            request->pkt->req->setExtraData(1);
        }
        //
        // Independent of success, all SC operations must clear the lock
        //
        e->clearLocked();
    } else if (request->m_type == RubyRequestType_Load_Linked) {
        //
        // Note: To fully follow Alpha LLSC semantics, should the LL clear any
        // previously locked cache lines?
        //
        e->setLocked(m_version);
    } else if (e->isLocked(m_version)) {
        //
        // Normal writes should clear the locked address
        //
        e->clearLocked();
    }
    return success;
}

void
Sequencer::recordMissLatency(const Cycles cycles, const RubyRequestType type,
                             const MachineType respondingMach,
                             bool isExternalHit, Cycles issuedTime,
                             Cycles initialRequestTime,
                             Cycles forwardRequestTime,
                             Cycles firstResponseTime, Cycles completionTime)
{
    m_latencyHist.sample(cycles);
    m_typeLatencyHist[type]->sample(cycles);

    if (isExternalHit) {
        m_missLatencyHist.sample(cycles);
        m_missTypeLatencyHist[type]->sample(cycles);

        if (respondingMach != MachineType_NUM) {
            m_missMachLatencyHist[respondingMach]->sample(cycles);
            m_missTypeMachLatencyHist[type][respondingMach]->sample(cycles);

            if ((issuedTime <= initialRequestTime) &&
                (initialRequestTime <= forwardRequestTime) &&
                (forwardRequestTime <= firstResponseTime) &&
                (firstResponseTime <= completionTime)) {

                m_IssueToInitialDelayHist[respondingMach]->sample(
                    initialRequestTime - issuedTime);
                m_InitialToForwardDelayHist[respondingMach]->sample(
                    forwardRequestTime - initialRequestTime);
                m_ForwardToFirstResponseDelayHist[respondingMach]->sample(
                    firstResponseTime - forwardRequestTime);
                m_FirstResponseToCompletionDelayHist[respondingMach]->sample(
                    completionTime - firstResponseTime);
            } else {
                m_IncompleteTimes[respondingMach]++;
            }
        }
    } else {
        m_hitLatencyHist.sample(cycles);
        m_hitTypeLatencyHist[type]->sample(cycles);

        if (respondingMach != MachineType_NUM) {
            m_hitMachLatencyHist[respondingMach]->sample(cycles);
            m_hitTypeMachLatencyHist[type][respondingMach]->sample(cycles);
        }
    }
}

void
Sequencer::writeCallback(Addr address, DataBlock& data,
                         const bool externalHit,
                         const bool hitAtL0, const bool hitAtL1, const bool hitAtL2, const bool hitAtMem,
                         const MachineType mach,
                         const Cycles initialRequestTime,
                         const Cycles forwardRequestTime,
                         const Cycles firstResponseTime)
{
    assert(address == makeLineAddress(address));
    assert(m_writeRequestTable.count(makeLineAddress(address)));

    RequestTable::iterator i = m_writeRequestTable.find(address);
    assert(i != m_writeRequestTable.end());
    SequencerRequest* request = i->second;

    m_writeRequestTable.erase(i);
    markRemoved();

    assert((request->m_type == RubyRequestType_ST) ||
           (request->m_type == RubyRequestType_ATOMIC) ||
           (request->m_type == RubyRequestType_RMW_Read) ||
           (request->m_type == RubyRequestType_RMW_Write) ||
           (request->m_type == RubyRequestType_Load_Linked) ||
           (request->m_type == RubyRequestType_Store_Conditional) ||
           (request->m_type == RubyRequestType_Locked_RMW_Read) ||
           (request->m_type == RubyRequestType_Locked_RMW_Write) ||
           (request->m_type == RubyRequestType_FLUSH));

    //
    // For Alpha, properly handle LL, SC, and write requests with respect to
    // locked cache blocks.
    //
    // Not valid for Garnet_standalone protocl
    //
    bool success = true;
    if (!m_runningGarnetStandalone)
        success = handleLlsc(address, request);

    // Handle SLICC block_on behavior for Locked_RMW accesses. NOTE: the
    // address variable here is assumed to be a line address, so when
    // blocking buffers, must check line addresses.
    if (request->m_type == RubyRequestType_Locked_RMW_Read) {
        // blockOnQueue blocks all first-level cache controller queues
        // waiting on memory accesses for the specified address that go to
        // the specified queue. In this case, a Locked_RMW_Write must go to
        // the mandatory_q before unblocking the first-level controller.
        // This will block standard loads, stores, ifetches, etc.
        m_controller->blockOnQueue(address, m_mandatory_q_ptr);
    } else if (request->m_type == RubyRequestType_Locked_RMW_Write) {
        m_controller->unblock(address);
    }

    hitCallback(request, data, success, mach, externalHit,
                initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong: remove SpecBuf related code
bool Sequencer::updateSBB(PacketPtr pkt, DataBlock& data, Addr address, bool data_hit) {
    assert(!RubySystem::getMLDOMEnabled() || RubySystem::getOblSContentionEnabled());
    // if the data is not a valid hit data, return immediately
    if (!data_hit)
        return false;

    SBE& sbe = m_specBuf[pkt->reqIdx];
    SBB& sbb = pkt->isFirst() ? sbe.blocks[0] : sbe.blocks[1];
    if (makeLineAddress(sbb.reqAddress) == address) {
        DPRINTFR(JY_Ruby, "[success] updateSBB for reqIdx=%d, addr=0x%lx, sn=%lli, ifFirst=%d, hit=%d\n",
            pkt->reqIdx, address, pkt->seqNum, pkt->isFirst(), data_hit);
        sbb.data = data;
        sbb.data_hit = data_hit;
        return true;
    }
    DPRINTFR(JY_Ruby, "[fail] updateSBB for reqIdx=%d, addr=0x%lx, sn=%lli, ifFirst=%d\n",
            pkt->reqIdx, address, pkt->seqNum, pkt->isFirst());
    return false;
}

// [SafeSpec] Called by Ruby to send a response to CPU.
void
Sequencer::readCallback(Addr address, DataBlock& data,
                        bool externalHit,
                        const bool hitAtL0, const bool hitAtL1, const bool hitAtL2, const bool hitAtMem,
                        const MachineType mach,
                        Cycles initialRequestTime,
                        Cycles forwardRequestTime,
                        Cycles firstResponseTime)
{
    assert(address == makeLineAddress(address));
    assert(m_readRequestTable.count(makeLineAddress(address)));

    RequestTable::iterator i = m_readRequestTable.find(address);
    assert(i != m_readRequestTable.end());
    SequencerRequest* request = i->second;

    m_readRequestTable.erase(i);
    markRemoved();

    assert((request->m_type == RubyRequestType_LD) ||
           (request->m_type == RubyRequestType_SPEC_LD) ||
           (request->m_type == RubyRequestType_EXPOSE) ||
           (request->m_type == RubyRequestType_IFETCH));

    PacketPtr pkt = request->pkt;

    DPRINTFR(JY_Ruby, "%10s readCallback remove request from RequestTable addr=%#x, sn=%lli)\n", curTick(), address, pkt->seqNum);

    // Jiyong, pass Hit Status to CPU
    if (hitAtL0) {
        pkt->setL0_Hit();
        pkt->fromLevel = 0;
    }
    else if (hitAtL1) {
        pkt->setL1_Hit();
        pkt->fromLevel = 1;
    }
    else if (hitAtL2) {
        pkt->setL2_Hit();
        pkt->fromLevel = 2;
    }
    else if (hitAtMem) {
        pkt->setMem_Hit();
        pkt->fromLevel = 3;
    }

    if (pkt->isSpec()) {
        assert(!pkt->onlyAccessSpecBuf());
        // Jiyong: remove SpecBuffer related code
        if (!RubySystem::getMLDOMEnabled) {
            DPRINTFR(SpecBuffer, "%10s SPEC_LD callback (idx=%d-%d, addr=%#x)\n", curTick(), pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
            updateSBB(pkt, data, address, true);
        }
        if (!externalHit) {
            pkt->setL0_Hit();
        }
    } else if (pkt->isExpose()) {
        // Jiyong: remove SpecBuffer related code
        if (!RubySystem::getMLDOMEnabled()) {
            DPRINTFR(SpecBuffer, "%10s EXPOSE callback (idx=%d-%d, addr=%#x)\n", curTick(), pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        }
    } else if (pkt->isValidate()) {
        // Jiyong: remove SpecBuffer related code
        if (!RubySystem::getMLDOMEnabled()) {
            DPRINTFR(SpecBuffer, "%10s VALIDATE callback (idx=%d-%d, addr=%#x)\n", curTick(), pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
            uint8_t idx = pkt->reqIdx;
            SBE& sbe = m_specBuf[idx];
            int blkIdx = pkt->isFirst() ? 0 : 1;
            SBB& sbb = sbe.blocks[blkIdx];
            assert(makeLineAddress(sbb.reqAddress) == address);
            if (!memcmp(sbb.data.getData(getOffset(pkt->getAddr()), pkt->getSize()), data.getData(getOffset(pkt->getAddr()), pkt->getSize()), pkt->getSize())) {
                *(pkt->getPtr<uint8_t>()) = 1;
            } else {
                // std::ostringstream os;
                // sbb.data.print(os);
                // DPRINTFR(SpecBufferValidate, "%s\n", os.str());
                // os.str("");
                // data.print(os);
                // DPRINTFR(SpecBufferValidate, "%s\n", os.str());
                *(pkt->getPtr<uint8_t>()) = 0;
            }
        }
    }

    if (RubySystem::getMLDOMEnabled() && request->m_type == RubyRequestType_LD) {
        for (auto& dependentPkt : request->dependentRequests) {
            if (dependentPkt->isExpose())
                DPRINTFR(JY_Ruby, "%10s Merged Expose callback (sn:%lli, idx=%d-%d, addr=%#x)\n", 
                        curTick(), dependentPkt->seqNum, dependentPkt->reqIdx, dependentPkt->isFirst()? 0 : 1, printAddress(dependentPkt->getAddr()));
            else if (dependentPkt->isValidate())
                DPRINTFR(JY_Ruby, "%10s Merged Validate callback (sn:%lli, idx=%d-%d, addr=%#x)\n", 
                        curTick(), dependentPkt->seqNum, dependentPkt->reqIdx, dependentPkt->isFirst()? 0 : 1, printAddress(dependentPkt->getAddr()));
            else if (dependentPkt->isSpec()) {
                DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                        curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                dependentPkt->fromLevel = 0;
                dependentPkt->isFinalPacket = true;
                dependentPkt->setL0_Hit();
            }
            else
                assert(0);

            memcpy(dependentPkt->getPtr<uint8_t>(),
                   data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                   dependentPkt->getSize());

            ruby_hit_callback(dependentPkt);
        }
    }

    hitCallback(request, data, true, mach, externalHit,
                initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong, MLDOM: readcallback for hit/miss in L0
void
Sequencer::readCallbackObliv_fromL0(Addr address,
                                    const bool hitAtL0,
                                    DataBlock& data_from_L0,
                                    const int  reqIdx,
                                    const MachineType mach,
                                    const Cycles initialRequestTime,
                                    const Cycles forwardRequestTime,
                                    const Cycles firstResponseTime)
{
    // for spec load callback, get the entry from specldRequestTable
    assert(address == makeLineAddress(address));

    SpecRequestTable::iterator i = m_specldRequestTable.find(std::make_pair(address, reqIdx));
    assert(i != m_specldRequestTable.end());
    SequencerRequest* request = i->second;

    PacketPtr pkt = request->pkt;

    if (!pkt->isSpec()) {
        DPRINTFR(JY_Ruby, "%10s ERROR: readCallbackObliv_fromL0: (sn=%lli, idx=%d-%d, addr=%#x) is not Spec\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        assert(0);
    }

    //Jiyong: for obls Contention simulation, update specbuffer with data on OblS return
    if (RubySystem::getOblSContentionEnabled())
        updateSBB(pkt, data_from_L0, address, hitAtL0);

    if (request->m_type == RubyRequestType_SPEC_LD_L0) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L0 commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x) --> last readCallback\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L1) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L1 commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L2) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L2 commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Mem) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Mem commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Perfect) {
        // remove the request if this perfect oblS hits L0 (since no future cache access occurs)
        if (hitAtL0) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Perfect commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_PerfectUnsafe) {
        // remove the request if this perfectUnsafe oblS hits L0 (since no future cache access occurs)
        if (hitAtL0) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_PerfectUnsafe commands callback readCallback_fromL0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else {
        DPRINTFR(JY_Ruby, "ERROR: readCallbackObliv_fromL0 is called by unknwon RubyRequestType: %s\n", request->m_type);
        assert(0);
    }

    DataBlock ret_data;

    if (hitAtL0) {
        pkt->setL0_Hit();
        ret_data = data_from_L0;
    }

    hitCallbackObliv(request, hitAtL0, ret_data, 0,  // 0 means the response is from L0
                true, mach, false, initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong, MLDOM: readcallback for hit/miss in L1
void
Sequencer::readCallbackObliv_fromL1(Addr address,
                                    const bool hitAtL1,
                                    DataBlock& data_from_L1,
                                    const int  reqIdx,
                                    const MachineType mach,
                                    const Cycles initialRequestTime,
                                    const Cycles forwardRequestTime,
                                    const Cycles firstResponseTime)
{
    assert(address == makeLineAddress(address));

    SpecRequestTable::iterator i = m_specldRequestTable.find(std::make_pair(address, reqIdx));
    assert(i != m_specldRequestTable.end());
    SequencerRequest* request = i->second;

    PacketPtr pkt = request->pkt;

    if (!pkt->isSpec()) {
        DPRINTFR(JY_Ruby, "%10s ERROR: readCallbackObliv_fromL1: (sn=%lli, idx=%d-%d, addr=%#x) is not Spec\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        assert(0);
    }

    //Jiyong: for obls Contention simulation, update specbuffer with data on OblS return
    if (RubySystem::getOblSContentionEnabled())
        updateSBB(pkt, data_from_L1, address, hitAtL1);

    if (request->m_type == RubyRequestType_SPEC_LD_L0) {
        printf("ERROR: SPEC_LD_L0 should not call readCallbackObliv_fromL1\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L1) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L1 commands callback readCallback_fromL1 (sn=%lli, idx=%d-%d, addr=%#x) --> last readCallback\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L2) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L2 commands callback readCallback_fromL1 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Mem) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Mem commands callback readCallback_fromL1 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Perfect) {
        // remove the request if this perfect oblS hits L1 (since no future cache access occurs)
        if (hitAtL1) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Perfect commands callback readCallback_fromL1 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_PerfectUnsafe) {
        // remove the request if this perfectUnsafe oblS hits L1 (since no future cache access occurs)
        if (hitAtL1) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_PerfectUnsafe commands callback readCallback_fromL1 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else {
        DPRINTFR(JY_Ruby, "ERROR: readCallbackObliv_fromL1 is called by unknwon RubyRequestType: %s\n", request->m_type);
        assert(0);
    }

    DataBlock ret_data;

    if (hitAtL1) {
        pkt->setL1_Hit();
        ret_data = data_from_L1;
    }

    hitCallbackObliv(request, hitAtL1, ret_data, 1, // 1 means the response is from L1
                true, mach, false, initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong, MLDOM: readcallback for hit/miss in L2
void
Sequencer::readCallbackObliv_fromL2(Addr address,
                                    const bool hitAtL2,
                                    DataBlock& data_from_L2,
                                    const int  reqIdx,
                                    const MachineType mach,
                                    const Cycles initialRequestTime,
                                    const Cycles forwardRequestTime,
                                    const Cycles firstResponseTime)
{
    assert(address == makeLineAddress(address));

    SpecRequestTable::iterator i = m_specldRequestTable.find(make_pair(address, reqIdx));
    assert(i != m_specldRequestTable.end());
    SequencerRequest* request = i->second;

    PacketPtr pkt = request->pkt;

    if (!pkt->isSpec()) {
        DPRINTFR(JY_Ruby, "%10s ERROR: readCallbackObliv_fromL2: (sn=%lli, idx=%d-%d, addr=%#x) is not Spec\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        assert(0);
    }

    //Jiyong: for obls Contention simulation, update specbuffer with data on OblS return
    if (RubySystem::getOblSContentionEnabled())
        updateSBB(pkt, data_from_L2, address, hitAtL2);

    if (request->m_type == RubyRequestType_SPEC_LD_L0) {
        printf("ERROR: SPEC_LD_L0 should not call readCallbackObliv_fromL2\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L1) {
        printf("ERROR: SPEC_LD_L1 should not call readCallbackObliv_fromL2\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L2) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_L2 commands callback readCallback_fromL2 (sn=%lli, idx=%d-%d, addr=%#x) --> last readCallback\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Mem) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Mem commands callback readCallback_fromL2 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Perfect) {
        // remove the request if this perfect oblS hits L2 (since no future cache access occurs)
        if (hitAtL2) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Perfect commands callback readCallback_fromL2 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_PerfectUnsafe) {
        // remove the request if this perfectUnsafe oblS hits L2 (since no future cache access occurs)
        if (hitAtL2) {
            m_specldRequestTable.erase(i);
            markRemoved();
        }
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_PerfectUnsafe commands callback readCallback_fromL2 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
    }
    else {
        DPRINTFR(JY_Ruby, "ERROR: readCallbackObliv_fromL2 is called by unknwon RubyRequestType: %s\n", request->m_type);
        assert(0);
    }

    DataBlock ret_data;

    if (hitAtL2) {
        pkt->setL2_Hit();
        ret_data = data_from_L2;
    }

    hitCallbackObliv(request, hitAtL2, ret_data, 2, // 2 means the response is from L2
                true, mach, false, initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong, MLDOM: readcallback for hit/miss in Mem
void
Sequencer::readCallbackObliv_fromMem(Addr address,
                                     const bool hitAtMem,
                                     DataBlock& data_from_Mem,
                                     const int  reqIdx,
                                     const MachineType mach,
                                     const Cycles initialRequestTime,
                                     const Cycles forwardRequestTime,
                                     const Cycles firstResponseTime)
{
    assert(address == makeLineAddress(address));

    SpecRequestTable::iterator i = m_specldRequestTable.find(std::make_pair(address, reqIdx));
    assert(i != m_specldRequestTable.end());
    SequencerRequest* request = i->second;

    PacketPtr pkt = request->pkt;

    if (!pkt->isSpec()) {
        DPRINTFR(JY_Ruby, "%10s ERROR: readCallbackObliv_fromMem: (sn=%lli, idx=%d-%d, addr=%#x) is not Spec\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        assert(0);
    }

    //Jiyong: for obls Contention simulation, update specbuffer with data on OblS return
    if (RubySystem::getOblSContentionEnabled())
        updateSBB(pkt, data_from_Mem, address, hitAtMem);

    if (request->m_type == RubyRequestType_SPEC_LD_L0) {
        printf("ERROR: SPEC_LD_L0 should not call readCallbackObliv_fromMem\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L1) {
        printf("ERROR: SPEC_LD_L1 should not call readCallbackObliv_fromMem\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_L2) {
        printf("ERROR: SPEC_LD_L2 should not call readCallbackObliv_fromMem\n");
        assert(0);
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Mem) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Mem commands callback readCallback_fromMem (sn=%lli, idx=%d-%d, addr=%#x) --> last readCallback\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_Perfect) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_Perfect commands callback readCallback_fromMem (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request (since no future cache access occurs)
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else if (request->m_type == RubyRequestType_SPEC_LD_PerfectUnsafe) {
        DPRINTFR(JY_Ruby, "%10s SPEC_LD_PerfectUnsafe commands callback readCallback_fromMem (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
        // remove the request (since no future cache access occurs)
        m_specldRequestTable.erase(i);
        markRemoved();
    }
    else {
        DPRINTFR(JY_Ruby, "ERROR: readCallbackObliv_fromL2 is called by unknwon RubyRequestType: %s\n", request->m_type);
        assert(0);
    }

    DataBlock ret_data;

    if (hitAtMem) {
        pkt->setMem_Hit();
        ret_data = data_from_Mem;
    }

    hitCallbackObliv(request, hitAtMem, ret_data, 3, // 3 means the response is from Memory
                true, mach, false, initialRequestTime, forwardRequestTime, firstResponseTime);
}

// Jiyong: remove SpecBuf related code
void
Sequencer::specBufHitCallback()
{
    assert(!RubySystem::getMLDOMEnabled() || RubySystem::getOblSContentionEnabled());
    assert(m_specRequestQueue.size());
    while (m_specRequestQueue.size()) {
        auto specReq = m_specRequestQueue.front();
        if (specReq.second <= curTick()) {
            PacketPtr pkt = specReq.first;
            assert(pkt->onlyAccessSpecBuf());
            DPRINTFR(SpecBuffer, "%10s SB Hit Callback (idx=%d, addr=%#x)\n", curTick(), pkt->reqIdx, printAddress(pkt->getAddr()));
            ruby_hit_callback(pkt);
            m_specRequestQueue.pop();
        } else {
            schedule(specBufHitEvent, specReq.second);
            break;
        }
    }
}

// [SafeSpec] Response on the way from Ruby to CPU
void
Sequencer::hitCallback(SequencerRequest* srequest, DataBlock& data,
                       bool llscSuccess,
                       const MachineType mach, const bool externalHit,
                       const Cycles initialRequestTime,
                       const Cycles forwardRequestTime,
                       const Cycles firstResponseTime)
{
    warn_once("Replacement policy updates recently became the responsibility "
              "of SLICC state machines. Make sure to setMRU() near callbacks "
              "in .sm files!");

    PacketPtr pkt = srequest->pkt;
    Addr request_address(pkt->getAddr());
    RubyRequestType type = srequest->m_type;
    Cycles issued_time = srequest->issue_time;

    assert(curCycle() >= issued_time);
    Cycles total_latency = curCycle() - issued_time;

    // Profile the latency for all demand accesses.
    recordMissLatency(total_latency, type, mach, externalHit, issued_time,
                      initialRequestTime, forwardRequestTime,
                      firstResponseTime, curCycle());

    DPRINTFR(ProtocolTrace, "%15s %3s %10s%20s %6s>%-6s %#x %d cycles\n",
             curTick(), m_version, "Seq",
             llscSuccess ? "Done" : "SC_Failed", "", "",
             printAddress(request_address), total_latency);

    // update the data unless it is a non-data-carrying flush
    if (RubySystem::getWarmupEnabled()) {
        data.setData(pkt->getConstPtr<uint8_t>(),
                     getOffset(request_address), pkt->getSize());
    } else if (!pkt->isFlush() && !pkt->isExpose() && !pkt->isValidate()) {
        if ((type == RubyRequestType_LD) ||
            (type == RubyRequestType_SPEC_LD) ||
            (type == RubyRequestType_SPEC_LD_L0) ||
            (type == RubyRequestType_SPEC_LD_L1) ||
            (type == RubyRequestType_SPEC_LD_L2) ||
            (type == RubyRequestType_SPEC_LD_Mem) ||
            (type == RubyRequestType_SPEC_LD_Perfect) ||
            (type == RubyRequestType_SPEC_LD_PerfectUnsafe) ||
            (type == RubyRequestType_IFETCH) ||
            (type == RubyRequestType_RMW_Read) ||
            (type == RubyRequestType_Locked_RMW_Read) ||
            (type == RubyRequestType_Load_Linked)) {
            memcpy(pkt->getPtr<uint8_t>(),
                   data.getData(getOffset(request_address), pkt->getSize()),
                   pkt->getSize());
            DPRINTF(RubySequencer, "read data %s\n", data);
        } else if (pkt->req->isSwap()) {
            std::vector<uint8_t> overwrite_val(pkt->getSize());
            memcpy(&overwrite_val[0], pkt->getConstPtr<uint8_t>(),
                   pkt->getSize());
            memcpy(pkt->getPtr<uint8_t>(),
                   data.getData(getOffset(request_address), pkt->getSize()),
                   pkt->getSize());
            data.setData(&overwrite_val[0],
                         getOffset(request_address), pkt->getSize());
            DPRINTF(RubySequencer, "swap data %s\n", data);
        } else if (type != RubyRequestType_Store_Conditional || llscSuccess) {
            // Types of stores set the actual data here, apart from
            // failed Store Conditional requests
            data.setData(pkt->getConstPtr<uint8_t>(),
                         getOffset(request_address), pkt->getSize());
            DPRINTF(RubySequencer, "set data %s\n", data);
        }
    }
    // Jiyong, MLDOM: pass the expose/validate data
    if (RubySystem::getMLDOMEnabled()) {
        if (pkt->isExpose() || pkt->isValidate()) {
            memcpy(pkt->getPtr<uint8_t>(),
                   data.getData(getOffset(request_address), pkt->getSize()),
                   pkt->getSize());
            DPRINTF(RubySequencer, "expose/validate data %s\n", data);
        }
    }

    // If using the RubyTester, update the RubyTester sender state's
    // subBlock with the recieved data.  The tester will later access
    // this state.
    if (m_usingRubyTester) {
        DPRINTF(RubySequencer, "hitCallback %s 0x%x using RubyTester\n",
                pkt->cmdString(), pkt->getAddr());
        RubyTester::SenderState* testerSenderState =
            pkt->findNextSenderState<RubyTester::SenderState>();
        assert(testerSenderState);
        testerSenderState->subBlock.mergeFrom(data);
    }

    delete srequest;

    RubySystem *rs = m_ruby_system;
    if (RubySystem::getWarmupEnabled()) {
        assert(pkt->req);
        delete pkt->req;
        delete pkt;
        rs->m_cache_recorder->enqueueNextFetchRequest();
    } else if (RubySystem::getCooldownEnabled()) {
        delete pkt;
        rs->m_cache_recorder->enqueueNextFlushRequest();
    } else {
        ruby_hit_callback(pkt);
        testDrainComplete();
    }
}

void
Sequencer::hitCallbackObliv(SequencerRequest* srequest, bool hit, DataBlock& data, int fromLevel,
                            bool llscSuccess,
                            const MachineType mach, const bool externalHit,
                            const Cycles initialRequestTime,
                            const Cycles forwardRequestTime,
                            const Cycles firstResponseTime)
{
    warn_once("Replacement policy updates recently became the responsibility "
              "of SLICC state machines. Make sure to setMRU() near callbacks "
              "in .sm files!");

    PacketPtr pkt = srequest->pkt;
    Addr request_address(pkt->getAddr());
    RubyRequestType type = srequest->m_type;
    Cycles issued_time = srequest->issue_time;

    assert(curCycle() >= issued_time);
    Cycles total_latency = curCycle() - issued_time;

    // Profile the latency for all demand accesses.
    recordMissLatency(total_latency, type, mach, externalHit, issued_time,
                      initialRequestTime, forwardRequestTime,
                      firstResponseTime, curCycle());

    assert(!pkt->isFlush() && !pkt->isExpose() && !pkt->isValidate());

    // If using the RubyTester, update the RubyTester sender state's
    // subBlock with the recieved data.  The tester will later access
    // this state.
    if (m_usingRubyTester) {
        DPRINTF(RubySequencer, "hitCallback %s 0x%x using RubyTester\n",
                pkt->cmdString(), pkt->getAddr());
        RubyTester::SenderState* testerSenderState =
            pkt->findNextSenderState<RubyTester::SenderState>();
        assert(testerSenderState);
        testerSenderState->subBlock.mergeFrom(data);
    }

    // update the data unless it is a non-data-carrying flush
    if (RubySystem::getWarmupEnabled()) {
        data.setData(pkt->getConstPtr<uint8_t>(), getOffset(request_address), pkt->getSize());
    }

    // Jiyong, MLDOM: create packets from multiple response of SpecLoads
    PacketPtr pkt_to_send = nullptr;
    if (type == RubyRequestType_SPEC_LD_L0) {
        if (fromLevel == 0) { // sent by L0, the last callback
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_L0 (idx=%d-%d, addr=%#x), hit=%d at L0, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit);
            // return the final, complete, original packet
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;

            // write and return the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                dependentPkt->fromLevel     = 0;
                dependentPkt->isFinalPacket = true;

                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }

                ruby_hit_callback(dependentPkt);
            }
            delete srequest;    // time to delete the srequest as it's the last hitcallback
        }
        else {
            printf("ERROR: RubyRequestType_SPEC_LD_L0 receives response from level %d\n", fromLevel);
            assert(0);
        }
    }
    else if (type == RubyRequestType_SPEC_LD_L1) {
        if (fromLevel < 1) { // sent by L0
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_L1 (idx=%d-%d, addr=%#x), hit=%d at L%d, return early packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            // return an intermediate packet
            pkt_to_send = new Packet(pkt, false, true);  // we copy the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = false;
            pkt_to_send->confirmPkt    = pkt;

            // write the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }
            }
        }
        else if (fromLevel == 1) { // sent by L1, the last callback
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_L1 (idx=%d-%d, addr=%#x), hit=%d at L1, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit);
            // return the final, complete, original packet
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;

            // write and return the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                dependentPkt->fromLevel     = 0;
                dependentPkt->isFinalPacket = true;

                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }

                ruby_hit_callback(dependentPkt);
            }
            delete srequest;    // time to delete the srequest as it's the last hitcallback
        }
        else {
            printf("ERROR: RubyRequestType_SPEC_LD_L1 receives response from level %d\n", fromLevel);
            assert(0);
        }
    }
    else if (type == RubyRequestType_SPEC_LD_L2) {
        if (fromLevel < 2) { // sent by L0 or L1
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_L2 (idx=%d-%d, addr=%#x), hit=%d at L%d, return early packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            // return an intermediate packet
            pkt_to_send = new Packet(pkt, false, true);  // we copy the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = false;
            pkt_to_send->confirmPkt    = pkt;

            // write the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }
            }
        }
        else if (fromLevel == 2) {
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_L2 (idx=%d-%d, addr=%#x), hit=%d at L2, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit);
            // return the final, complet, original packet
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;

            // write and return the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                dependentPkt->fromLevel     = 0;
                dependentPkt->isFinalPacket = true;

                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }

                ruby_hit_callback(dependentPkt);
            }
            delete srequest;
        }
        else {
            printf("ERROR: RubyRequestType_SPEC_LD_L2 receives response from level %d\n", fromLevel);
            assert(0);
        }
    }
    else if (type == RubyRequestType_SPEC_LD_Mem) {
        if (fromLevel < 3) { // sent by L0 or L1 or L2
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_Mem (idx=%d-%d, addr=%#x), hit=%d at L%d, return early packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            pkt_to_send = new Packet(pkt, false, true);  // we copy the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = false;
            pkt_to_send->confirmPkt    = pkt;

            // write the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }
            }
        }
        else if (fromLevel == 3) {
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_Mem (idx=%d-%d, addr=%#x), hit=%d at Mem, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit);
            // return the final, complete, original packet
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;

            // write and return the dependentPkt if any
            for (auto& dependentPkt : srequest->dependentRequests) {
                dependentPkt->fromLevel     = 0;
                dependentPkt->isFinalPacket = true;

                if (hit && makeLineAddress(pkt->getAddr()) == makeLineAddress(dependentPkt->getAddr())) {
                    DPRINTF(JY_Ruby, "%10s pkt [sn=%lli, addr=%#x] also writes its dependent pkt [sn=%lli, addr=%#x]\n",
                            curTick(), pkt->seqNum, pkt->getAddr(), dependentPkt->seqNum, dependentPkt->getAddr());
                    dependentPkt->setL0_Hit();
                    memcpy(dependentPkt->getPtr<uint8_t>(),
                           data.getData(getOffset(dependentPkt->getAddr()), dependentPkt->getSize()),
                           dependentPkt->getSize());
                }

                ruby_hit_callback(dependentPkt);
            }
            delete srequest;
        }
        else {
            printf("ERROR: RubyRequestType_SPEC_LD_L2 receives response from level %d\n", fromLevel);
            assert(0);
        }
    }
    else if (type == RubyRequestType_SPEC_LD_Perfect) {
        if (hit || fromLevel == 3) {    // send the original packet if it hits or the packet is from memory
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_Perfect (idx=%d-%d, addr=%#x), hit=%d at L%d, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;
            delete srequest;
        }
        else {
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_Perfect (idx=%d-%d, addr=%#x), hit=%d at L%d, return early packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            pkt_to_send = new Packet(pkt, false, true);  // we copy the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = false;
            pkt_to_send->confirmPkt    = pkt;
        }
    }
    else if (type == RubyRequestType_SPEC_LD_PerfectUnsafe) {
        if (hit || fromLevel == 3) {    // send the original packet if it hits or the packet is from memory
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_PerfectUnsafe (idx=%d-%d, addr=%#x), hit=%d at L%d, return original packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            pkt_to_send = pkt;  // send the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = true;
            pkt_to_send->confirmPkt    = pkt;
            delete srequest;
        }
        else {
            DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD_PerfectUnsafe (idx=%d-%d, addr=%#x), hit=%d at L%d, return early packet\n", pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), hit, fromLevel);
            pkt_to_send = new Packet(pkt, false, true);  // we copy the original packet
            pkt_to_send->fromLevel     = fromLevel;
            pkt_to_send->isFinalPacket = false;
            pkt_to_send->confirmPkt    = pkt;
        }
    }
    else {
        printf("ERROR: unknown RubyRequestType\n");
        assert(0);
    }

    DPRINTF(JY_Ruby, "hitCallbackObliv: pkt_to_send(sn=%lli) froLevel=%d, finalPkt=%d, mhas HitL0 = %d, HitL1 = %d, HitL2 = %d, HitMem = %d\n", 
            pkt_to_send->seqNum, pkt_to_send->fromLevel, pkt_to_send->isFinalPacket, pkt_to_send->isL0_Hit(), pkt_to_send->isL1_Hit(), pkt_to_send->isL2_Hit(), pkt_to_send->isMem_Hit());
    DPRINTF(JY_Ruby, "hitCallbackObliv: main pkt(sn=%lli) has HitL0 = %d, HitL1 = %d, HitL2 = %d, HitMem = %d\n", 
            pkt->seqNum, pkt->isL0_Hit(), pkt->isL1_Hit(), pkt->isL2_Hit(), pkt->isMem_Hit());

    // sanity check
    assert(pkt->isSpec());

    if (hit && !pkt->copyData) {
        DPRINTF(JY_Ruby, "hitCallbackObliv: SPEC_LD (sn=%lli, idx=%d-%d, addr=%#x), at Level%d (first hit). Transfer data from Ruby to packet time_to CPU\n", pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()), fromLevel);
        memcpy(pkt->getPtr<uint8_t>(),
               data.getData(getOffset(request_address), pkt->getSize()),
               pkt->getSize());
        DPRINTF(JY_Ruby, "   read data %s\n", data);
        pkt->copyData = true;
        pkt_to_send->carryData = true;  // only 1 packet will carry data to the pipeline

        for (int i = 0; i < pkt->getSize(); i++)    // pkt and pkt_to_send should have identical data
            assert(pkt->getPtr<uint8_t>()[i] == pkt_to_send->getPtr<uint8_t>()[i]);
    }

    //delete srequest;

    RubySystem *rs = m_ruby_system;
    if (RubySystem::getWarmupEnabled()) {
        printf("??? get warmup?\n");
        assert(pkt->req);
        delete pkt->req;
        delete pkt;
        rs->m_cache_recorder->enqueueNextFetchRequest();
    } else if (RubySystem::getCooldownEnabled()) {
        printf("??? get Cooldown?\n");
        delete pkt;
        rs->m_cache_recorder->enqueueNextFlushRequest();
    } else {
        DPRINTF(JY_Ruby, "send pkt_to_send\n");
        ruby_hit_callback(pkt_to_send); // send the pkt_to_send
        testDrainComplete();
    }
}

bool
Sequencer::empty() const
{
    return m_writeRequestTable.empty() && m_readRequestTable.empty() && m_specldRequestTable.empty();
}

// [SafeSpec] Request on the way from CPU to Ruby
RequestStatus
Sequencer::makeRequest(PacketPtr pkt)
{
    DPRINTF(JY_Ruby, "makeRequest\n");
    if (m_outstanding_count >= m_max_outstanding_requests) {
        return RequestStatus_BufferFull;
    }

    RubyRequestType primary_type = RubyRequestType_NULL;
    RubyRequestType secondary_type = RubyRequestType_NULL;

    // Jiyong: notice we allow spec buffer hit for both regular ld request and specld request
    // This is incorrect since regular ld shouldn't use specbuffer
    // only for test purpose
    if (RubySystem::getOblSContentionEnabled()) {
        // create specbuffer entry for all requests
        SBE& sbe = m_specBuf[pkt->reqIdx];
        SBB& sbb = pkt->isFirst() ? sbe.blocks[0] : sbe.blocks[1];

        Addr reqAddr = pkt->getAddr();
        unsigned reqSize = pkt->getSize();

        if (pkt->isSpec()) {
            sbe.isSplit = pkt->isSplit;
            sbb.reqAddress = pkt->getAddr();
            sbb.reqSize = pkt->getSize();
            sbb.data_hit = false;
            DPRINTF(JY_Ruby, "[create sbb] reqAddr=%#x, reqIdx=%d, isFirst=%d\n",
                    makeLineAddress(sbb.reqAddress), pkt->reqIdx, pkt->isFirst());
        }

        if (pkt->onlyAccessSpecBuf()) { // specld and normal load can access spec buffer
            assert (RubySystem::getOblSContentionEnabled());

            SBE& src_sbe = m_specBuf[pkt->srcIdx];
            DPRINTF(JY_Ruby, "reqAddr=%#x, srcIdx=%d, src_blk0_addr=%#x, src_blk1_addr=%#x\n",
                    makeLineAddress(reqAddr), pkt->srcIdx, makeLineAddress(src_sbe.blocks[0].reqAddress), makeLineAddress(src_sbe.blocks[1].reqAddress));

            if (makeLineAddress(reqAddr) == makeLineAddress(src_sbe.blocks[0].reqAddress)) {
                if (!src_sbe.blocks[0].data_hit) { // specbuf entry must be valid
                    DPRINTFR(JY_Ruby, "data is not ready\n");
                    return RequestStatus_Aliased;
                }

                sbb.data = src_sbe.blocks[0].data;
                sbb.data_hit = true;

                memcpy(pkt->getPtr<uint8_t>(),
                       sbb.data.getData(getOffset(reqAddr), reqSize),
                       reqSize);
                pkt->setSB_Hit();
                m_specRequestQueue.push({pkt, curTick()});
                DPRINTF(JY_Ruby, "%10s SB hit for ld (sn=%lli, idx=%d, addr=%#x) on (srcIdx=%d)\n",
                        curTick(), pkt->seqNum, pkt->reqIdx, printAddress(reqAddr), pkt->srcIdx);
                numSpecBufferHit++;
                if (!specBufHitEvent.scheduled()) {
                    schedule(specBufHitEvent, clockEdge(Cycles(1)));
                }
                return RequestStatus_Issued;
            }
            else if (makeLineAddress(reqAddr) == makeLineAddress(src_sbe.blocks[1].reqAddress)) {
                if (src_sbe.blocks[1].data_hit) { // specbuf entry must be valid
                    DPRINTFR(JY_Ruby, "data is not ready\n");
                    return RequestStatus_Aliased;
                }

                sbb.data = src_sbe.blocks[1].data;
                sbb.data_hit = true;

                memcpy(pkt->getPtr<uint8_t>(),
                       sbb.data.getData(getOffset(reqAddr), reqSize),
                       reqSize);
                pkt->setSB_Hit();
                m_specRequestQueue.push({pkt, curTick()});
                DPRINTF(JY_Ruby, "%10s SB hit for ld (sn=%lli, idx=%d, addr=%#x) on (srcidx=%d)\n",
                        curTick(), pkt->seqNum, pkt->reqIdx, printAddress(reqAddr), pkt->srcIdx);
                numSpecBufferHit++;
                if (!specBufHitEvent.scheduled()) {
                    schedule(specBufHitEvent, clockEdge(Cycles(1)));
                }
                return RequestStatus_Issued;
            }
            else
                fatal("Requested address %#x for sn=%lli is not present in the spec buffer (supposed to be at %d)\n",
                        printAddress(reqAddr), pkt->seqNum, pkt->srcIdx);

            assert (!sbb.data_hit);
        }
        // check if hit on squashed entry
        if (pkt->isRead() && !pkt->isSpec()) {
            for (int i = 0; i < m_specBuf.size(); i++) {
                SBE& src_sbe = m_specBuf[i];
                if (makeLineAddress(reqAddr) == makeLineAddress(src_sbe.blocks[0].reqAddress) && src_sbe.blocks[0].data_hit) {
                    numPotentialMissedSpecBufferHit++;
                    DPRINTFR(JY_Ruby, "potential missed hit for sn=%lli\n", pkt->seqNum);
                    break;
                }
                else if (makeLineAddress(reqAddr) == makeLineAddress(src_sbe.blocks[1].reqAddress) && src_sbe.blocks[1].data_hit) {
                    numPotentialMissedSpecBufferHit++;
                    DPRINTFR(JY_Ruby, "potential missed hit for sn=%lli\n", pkt->seqNum);
                    break;
                }
            }
        }
    }
    // [SafeSpec] Handle new requests
    if (pkt->isSpec()) {
        // Jiyong, MLDOM: Specbuffer for simulation the effect of removing obls contention
        if (!RubySystem::getMLDOMEnabled()) {
            // Jiyong: Naive Invisispec mode
            assert(pkt->cmd == MemCmd::ReadSpecReq);
            assert(pkt->isSplit || pkt->isFirst());
            uint8_t idx = pkt->reqIdx;
            SBE& sbe = m_specBuf[idx];
            sbe.isSplit = pkt->isSplit;
            int blkIdx = pkt->isFirst() ? 0 : 1;
            SBB& sbb = sbe.blocks[blkIdx];
            sbb.reqAddress = pkt->getAddr();
            sbb.reqSize = pkt->getSize();
            if (pkt->onlyAccessSpecBuf()) {
                int srcIdx = pkt->srcIdx;
                SBE& srcEntry = m_specBuf[srcIdx];
                if (makeLineAddress(sbb.reqAddress) == makeLineAddress(srcEntry.blocks[0].reqAddress)) {
                    sbb.data = srcEntry.blocks[0].data;
                } else if (makeLineAddress(sbb.reqAddress) == makeLineAddress(srcEntry.blocks[1].reqAddress)) {
                    sbb.data = srcEntry.blocks[1].data;
                } else {
                    fatal("Requested address %#x is not present in the spec buffer\n", printAddress(sbb.reqAddress));
                }
                memcpy(pkt->getPtr<uint8_t>(),
                       sbb.data.getData(getOffset(sbb.reqAddress), sbb.reqSize),
                       sbb.reqSize);
                m_specRequestQueue.push({pkt, curTick()});
                DPRINTFR(SpecBuffer, "%10s SB Hit (idx=%d, addr=%#x) on (srcIdx=%d)\n", curTick(), idx, printAddress(sbb.reqAddress), srcIdx);
                if (!specBufHitEvent.scheduled()) {
                    schedule(specBufHitEvent, clockEdge(Cycles(1)));
                }
                return RequestStatus_Issued;
            } else {
                // assert it is not in the buffer
                primary_type = secondary_type = RubyRequestType_SPEC_LD;
            }
            // Jiyong: InvisiSpec code ends
        } else {
            // Jiyong: differentiate different ruby request types
            if (pkt->cmd == MemCmd::ReadSpecL0Req)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_L0;
            else if (pkt->cmd == MemCmd::ReadSpecL1Req)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_L1;
            else if (pkt->cmd == MemCmd::ReadSpecL2Req)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_L2;
            else if (pkt->cmd == MemCmd::ReadSpecMemReq)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_Mem;
            else if (pkt->cmd == MemCmd::ReadSpecPerfectReq)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_Perfect;
            else if (pkt->cmd == MemCmd::ReadSpecPerfectUnsafeReq)
                primary_type = secondary_type = RubyRequestType_SPEC_LD_PerfectUnsafe;
            else {
                assert(0);
                printf("ERROR: Unknown request type (in Sequencer::makeRequest)");
            }
        }
    } else if (pkt->isExpose() || pkt->isValidate()) {
        assert(pkt->cmd == MemCmd::ExposeReq || pkt->cmd == MemCmd::ValidateReq);
        assert(pkt->isSplit || pkt->isFirst());
        // Jiyong, MLDOM: remove all SpecBuffer related code
        if (!RubySystem::getMLDOMEnabled()) {
            uint8_t idx = pkt->reqIdx;
            SBE& sbe = m_specBuf[idx];
            sbe.isSplit = pkt->isSplit;
            int blkIdx = pkt->isFirst() ? 0 : 1;
            SBB& sbb = sbe.blocks[blkIdx];
            if (sbb.reqAddress != pkt->getAddr()) {
                fatal("sbb.reqAddress != pkt->getAddr: %#x != %#x\n", printAddress(sbb.reqAddress), printAddress(pkt->getAddr()));
            }
            if (sbb.reqSize != pkt->getSize()) {
                fatal("sbb.reqSize != pkt->getSize(): %d != %d\n", sbb.reqSize, pkt->getSize());
            }
            primary_type = secondary_type = RubyRequestType_EXPOSE;
        }
        else {
            // in MLDOM, we treat expose as normal load
            primary_type = secondary_type = RubyRequestType_LD;
        }
    } else if (pkt->isLLSC()) {
        //
        // Alpha LL/SC instructions need to be handled carefully by the cache
        // coherence protocol to ensure they follow the proper semantics. In
        // particular, by identifying the operations as atomic, the protocol
        // should understand that migratory sharing optimizations should not
        // be performed (i.e. a load between the LL and SC should not steal
        // away exclusive permission).
        //
        if (pkt->isWrite()) {
            DPRINTF(RubySequencer, "Issuing SC\n");
            primary_type = RubyRequestType_Store_Conditional;
        } else {
            DPRINTF(RubySequencer, "Issuing LL\n");
            assert(pkt->isRead());
            primary_type = RubyRequestType_Load_Linked;
        }
        secondary_type = RubyRequestType_ATOMIC;
    } else if (pkt->req->isLockedRMW()) {
        //
        // x86 locked instructions are translated to store cache coherence
        // requests because these requests should always be treated as read
        // exclusive operations and should leverage any migratory sharing
        // optimization built into the protocol.
        //
        if (pkt->isWrite()) {
            DPRINTF(RubySequencer, "Issuing Locked RMW Write\n");
            primary_type = RubyRequestType_Locked_RMW_Write;
        } else {
            DPRINTF(RubySequencer, "Issuing Locked RMW Read\n");
            assert(pkt->isRead());
            primary_type = RubyRequestType_Locked_RMW_Read;
        }
        secondary_type = RubyRequestType_ST;
    } else {
        //
        // To support SwapReq, we need to check isWrite() first: a SwapReq
        // should always be treated like a write, but since a SwapReq implies
        // both isWrite() and isRead() are true, check isWrite() first here.
        //
        if (pkt->isWrite()) {
            //
            // Note: M5 packets do not differentiate ST from RMW_Write
            //
            primary_type = secondary_type = RubyRequestType_ST;
        } else if (pkt->isRead()) {
            if (pkt->req->isInstFetch()) {
                primary_type = secondary_type = RubyRequestType_IFETCH;
            } else {
                bool storeCheck = false;
                // only X86 need the store check
                if (system->getArch() == Arch::X86ISA) {
                    uint32_t flags = pkt->req->getFlags();
                    storeCheck = flags &
                        (X86ISA::StoreCheck << X86ISA::FlagShift);
                }
                if (storeCheck) {
                    primary_type = RubyRequestType_RMW_Read;
                    secondary_type = RubyRequestType_ST;
                } else {
                    primary_type = secondary_type = RubyRequestType_LD;
                    DPRINTFR(JY_Ruby, "%10s Issuing read (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));
                }
            }
        } else if (pkt->isFlush()) {
          primary_type = secondary_type = RubyRequestType_FLUSH;
        } else {
            panic("Unsupported ruby packet type\n");
        }
    }

    RequestStatus status;
    if (RubySystem::getMLDOMEnabled()) {
        if (pkt->isSpecL0() || pkt->isSpecL1() || pkt->isSpecL2() || pkt->isSpecMem() || pkt->isSpecPerfect() || pkt->isSpecPerfectUnsafe()) {
            DPRINTF(JY_Ruby, "try inserting specld pkt [sn:%lli] to specldRequestTable\n", pkt->seqNum);
            status = insertSpecldRequest(pkt, primary_type);
        }
        else {
            if (pkt->isExpose())
                DPRINTF(JY_Ruby, "try inserting expose pkt [sn:%lli] to readRequestTable\n", pkt->seqNum);
            if (pkt->isValidate())
                DPRINTF(JY_Ruby, "try inserting validate pkt [sn:%lli] to readRequestTable\n", pkt->seqNum);
            status = insertRequest(pkt, primary_type);
        }
    }
    else {
        status = insertRequest(pkt, primary_type);
    }

    if (status == RequestStatus_Merged) {
        //assert (pkt->isExpose() || pkt->isValidate() || pkt->isSpecL1());
        return RequestStatus_Issued;
    } else if (status != RequestStatus_Ready) {
        return status;
    }

    if (pkt->isSpec())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isExpose())
        DPRINTFR(JY_Ruby, "%10s Issuing EXPOSE (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isValidate())
        DPRINTFR(JY_Ruby, "%10s Issuing VALIDATE (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecL0())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_L0 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecL1())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_L1 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecL2())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_L2 (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecMem())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_Mem (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecPerfect())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_Perfect (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    if (pkt->isSpecPerfectUnsafe())
        DPRINTFR(JY_Ruby, "%10s Issuing SPEC_LD_PerfectUnsafe (sn=%lli, idx=%d-%d, addr=%#x)\n", curTick(), pkt->seqNum, pkt->reqIdx, pkt->isFirst()? 0 : 1, printAddress(pkt->getAddr()));

    issueRequest(pkt, secondary_type);

    // TODO: issue hardware prefetches here
    return RequestStatus_Issued;
}

void
Sequencer::issueRequest(PacketPtr pkt, RubyRequestType secondary_type)
{
    assert(pkt != NULL);
    ContextID proc_id = pkt->req->hasContextId() ?
        pkt->req->contextId() : InvalidContextID;

    ContextID core_id = coreId();

    // If valid, copy the pc to the ruby request
    Addr pc = 0;
    if (pkt->req->hasPC()) {
        pc = pkt->req->getPC();
    }

    // check if the packet has data as for example prefetch and flush
    // requests do not
    std::shared_ptr<RubyRequest> msg =
        std::make_shared<RubyRequest>(clockEdge(), pkt->getAddr(),
                                      pkt->isFlush() || pkt->isExpose() ?
                                      nullptr : pkt->getPtr<uint8_t>(),
                                      pkt->getSize(), pc, secondary_type,
                                      RubyAccessMode_Supervisor, pkt,
                                      PrefetchBit_No, proc_id, core_id);

    DPRINTFR(ProtocolTrace, "%15s %3s %10s%20s %6s>%-6s %#x %s\n",
            curTick(), m_version, "Seq", "Begin", "", "",
            printAddress(msg->getPhysicalAddress()),
            RubyRequestType_to_string(secondary_type));

    // The Sequencer currently assesses instruction and data cache hit latency
    // for the top-level caches at the beginning of a memory access.
    // TODO: Eventually, this latency should be moved to represent the actual
    // cache access latency portion of the memory access. This will require
    // changing cache controller protocol files to assess the latency on the
    // access response path.
    Cycles latency(0);  // Initialize to zero to catch misconfigured latency

    // Jiyong, MLDOM: we will actually simulate the cache latency with CacheMemory, so here we make it 1 cycle
    if (secondary_type == RubyRequestType_IFETCH)
        latency = m_inst_cache_hit_latency;
    else
        latency = m_data_cache_hit_latency;
    //latency = 1;

    // Send the message to the cache controller
    assert(latency > 0);

    assert(m_mandatory_q_ptr != NULL);
    m_mandatory_q_ptr->enqueue(msg, clockEdge(), cyclesToTicks(latency));
}

template <class KEY, class VALUE>
std::ostream &
operator<<(ostream &out, const std::unordered_map<KEY, VALUE> &map)
{
    auto i = map.begin();
    auto end = map.end();

    out << "[";
    for (; i != end; ++i)
        out << " " << i->first << "=" << i->second;
    out << " ]";

    return out;
}

void
Sequencer::print(ostream& out) const
{
    out << "[Sequencer: " << m_version
        << ", outstanding requests: " << m_outstanding_count
        << ", read request table: " << m_readRequestTable
        << ", write request table: " << m_writeRequestTable
        << "]";
}

// this can be called from setState whenever coherence permissions are
// upgraded when invoked, coherence violations will be checked for the
// given block
void
Sequencer::checkCoherence(Addr addr)
{
#ifdef CHECK_COHERENCE
    m_ruby_system->checkGlobalCoherenceInvariant(addr);
#endif
}

void
Sequencer::recordRequestType(SequencerRequestType requestType) {
    DPRINTF(RubyStats, "Recorded statistic: %s\n",
            SequencerRequestType_to_string(requestType));
}


void
Sequencer::evictionCallback(Addr address, bool external)
{
    ruby_eviction_callback(address, external);
}

void
Sequencer::regStats()
{
    RubyPort::regStats();

    m_store_waiting_on_load
        .name(name() + ".store_waiting_on_load")
        .desc("Number of times a store aliased with a pending load")
        .flags(Stats::nozero);
    m_store_waiting_on_store
        .name(name() + ".store_waiting_on_store")
        .desc("Number of times a store aliased with a pending store")
        .flags(Stats::nozero);
    m_load_waiting_on_load
        .name(name() + ".load_waiting_on_load")
        .desc("Number of times a load aliased with a pending load")
        .flags(Stats::nozero);
    m_load_waiting_on_store
        .name(name() + ".load_waiting_on_store")
        .desc("Number of times a load aliased with a pending store")
        .flags(Stats::nozero);
    numSpecBufferHit
        .name(name() + ".numSpecBufferHit")
        .desc("Number of times a specld hits spec buffer (only possible if obls_contention is enabled[SDO]");
    numPotentialMissedSpecBufferHit
        .name(name() + ".numPotentialMissedSpecBufferHit")
        .desc("Number of times a specld hits (squashed) spec buffer [SDO]");

    // These statistical variables are not for display.
    // The profiler will collate these across different
    // sequencers and display those collated statistics.
    m_outstandReqHist.init(10);
    m_latencyHist.init(10);
    m_hitLatencyHist.init(10);
    m_missLatencyHist.init(10);

    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_typeLatencyHist.push_back(new Stats::Histogram());
        m_typeLatencyHist[i]->init(10);

        m_hitTypeLatencyHist.push_back(new Stats::Histogram());
        m_hitTypeLatencyHist[i]->init(10);

        m_missTypeLatencyHist.push_back(new Stats::Histogram());
        m_missTypeLatencyHist[i]->init(10);
    }

    for (int i = 0; i < MachineType_NUM; i++) {
        m_hitMachLatencyHist.push_back(new Stats::Histogram());
        m_hitMachLatencyHist[i]->init(10);

        m_missMachLatencyHist.push_back(new Stats::Histogram());
        m_missMachLatencyHist[i]->init(10);

        m_IssueToInitialDelayHist.push_back(new Stats::Histogram());
        m_IssueToInitialDelayHist[i]->init(10);

        m_InitialToForwardDelayHist.push_back(new Stats::Histogram());
        m_InitialToForwardDelayHist[i]->init(10);

        m_ForwardToFirstResponseDelayHist.push_back(new Stats::Histogram());
        m_ForwardToFirstResponseDelayHist[i]->init(10);

        m_FirstResponseToCompletionDelayHist.push_back(new Stats::Histogram());
        m_FirstResponseToCompletionDelayHist[i]->init(10);
    }

    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_hitTypeMachLatencyHist.push_back(std::vector<Stats::Histogram *>());
        m_missTypeMachLatencyHist.push_back(std::vector<Stats::Histogram *>());

        for (int j = 0; j < MachineType_NUM; j++) {
            m_hitTypeMachLatencyHist[i].push_back(new Stats::Histogram());
            m_hitTypeMachLatencyHist[i][j]->init(10);

            m_missTypeMachLatencyHist[i].push_back(new Stats::Histogram());
            m_missTypeMachLatencyHist[i][j]->init(10);
        }
    }
}
