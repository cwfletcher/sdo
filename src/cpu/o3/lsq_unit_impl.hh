/*
 * Copyright (c) 2010-2014, 2017 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
 *
 * Authors: Kevin Lim
 *          Korey Sewell
 */

#ifndef __CPU_O3_LSQ_UNIT_IMPL_HH__
#define __CPU_O3_LSQ_UNIT_IMPL_HH__

#include "arch/generic/debugfaults.hh"
#include "arch/locked_mem.hh"
#include "base/str.hh"
#include "config/the_isa.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/o3/lsq.hh"
#include "cpu/o3/lsq_unit.hh"
#include "cpu/o3/locPred.hh"
#include "debug/Activity.hh"
#include "debug/IEW.hh"
#include "debug/LSQUnit.hh"
#include "debug/JY.hh"
#include "debug/JY_SDO_Pred.hh"
#include "debug/O3PipeView.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

template<class Impl>
LSQUnit<Impl>::WritebackEvent::WritebackEvent(DynInstPtr &_inst, PacketPtr _pkt,
                                              LSQUnit *lsq_ptr)
    : Event(Default_Pri, AutoDelete),
      inst(_inst), pkt(_pkt), lsqPtr(lsq_ptr)
{
}

template<class Impl>
void
LSQUnit<Impl>::WritebackEvent::process()
{
    DPRINTF(JY, "WritebackEvent::process() is called for inst [sn:%lli], with pkt: sn=%lli, fromLevel=%d, finalPkt=%d, carryData=%d, isspec=%d, isValidate = %d\n",
            inst->seqNum, pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData, pkt->isSpec(), pkt->isValidate());
    assert(!lsqPtr->cpu->switchedOut());

    lsqPtr->writeback(inst, pkt);

    if (pkt->senderState)
        delete pkt->senderState;

    if (!pkt->isValidate() && !pkt->isExpose()){
        delete pkt->req;
    }
    delete pkt;
}

template<class Impl>
const char *
LSQUnit<Impl>::WritebackEvent::description() const
{
    return "Store writeback";
}


int max(int a, int b) {
    return (a > b)? a : b;
}
// [SafeSpec] This function deals with
// acknowledge response to memory read/write
template<class Impl>
void
LSQUnit<Impl>::completeDataAccess(PacketPtr pkt)
{
    LSQSenderState *state = dynamic_cast<LSQSenderState *>(pkt->senderState);
    if (!state){
        DPRINTF(JY, "a packet [sn:%lli] from Level %d, is1st = %d, is_final_packet = %d, carryData = %d, isSpec=%d HAS NOT STATE!\n", pkt->seqNum, pkt->fromLevel, pkt->isFirst(), pkt->isFinalPacket, pkt->carryData, pkt->isSpec());
        assert(0);
    }
    else
        DPRINTF(JY, "a packet [sn:%lli] from Level %d, is1st = %d, is_final_packet = %d, carryData = %d, isSpec=%d has state.\n", pkt->seqNum, pkt->fromLevel, pkt->isFirst(), pkt->isFinalPacket, pkt->carryData, pkt->isSpec());

    DynInstPtr inst = state->inst;
    DPRINTF(IEW, "Writeback event [sn:%lli].\n", inst->seqNum);
    DPRINTF(Activity, "Activity: Writeback event [sn:%lli].\n", inst->seqNum);

    if (inst->isLoad() && pkt->isSpec()) {
        if (pkt->isSpecL0())
            DPRINTF(JY, "a packet [sn:%lli] SpecLD_L0 from Level %d, is_final_packet = %d, carryData = %d\n", pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData);
        else if (pkt->isSpecL1())
            DPRINTF(JY, "a packet [sn:%lli] SpecLD_L1 from Level %d, is_final_packet = %d, carryData = %d\n", pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData);
        else if (pkt->isSpecL2())
            DPRINTF(JY, "a packet [sn:%lli] SpecLD_L2 from Level %d, is_final_packet = %d, carryData = %d\n", pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData);
        else if (pkt->isSpecMem())
            DPRINTF(JY, "a packet [sn:%lli] SpecLD_Mem from Level %d, is_final_packet = %d, carryData = %d\n", pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData);
        else if (pkt->isSpecPerfect())
            DPRINTF(JY, "a packet [sn:%lli] SpecLD_Perfect from Level %d, is_final_packet = %d, carryData = %d\n", pkt->seqNum, pkt->fromLevel, pkt->isFinalPacket, pkt->carryData);
    }
    else if (inst->isLoad() && pkt->isExpose())
        DPRINTF(JY, "a expose packet for [sn:%lli]\n", pkt->seqNum);
    else if (inst->isLoad() && pkt->isValidate())
        DPRINTF(JY, "a validate packet for [sn:%lli]\n", pkt->seqNum);

    DPRINTF(JY, "recv a Packet [sn:%lli] with HitSB = %d, HitL0 = %d, HitL1 = %d, HitL2 = %d, HitMem = %d\n", 
            pkt->seqNum, pkt->isSB_Hit(), pkt->isL0_Hit(), pkt->isL1_Hit(), pkt->isL2_Hit(), pkt->isMem_Hit());

    // Jiyong, set up cache hit status
    if (inst->isLoad() && !inst->needPostFetch()) {   // pkt is a non-spec load
        if (state->isSplit) {
            if (pkt->isFirst()) {
                if      (pkt->isL0_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 0);
                else if (pkt->isL1_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 1);
                else if (pkt->isL2_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 2);
                else if (pkt->isMem_Hit())    inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 3);
            }
            else {
                if      (pkt->isL0_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 0);
                else if (pkt->isL1_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 1);
                else if (pkt->isL2_Hit())     inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 2);
                else if (pkt->isMem_Hit())    inst->regLd_Hit_Level = max(inst->regLd_Hit_Level, 3);
            }
        }
        else {
            if      (pkt->isL0_Hit())         inst->regLd_Hit_Level = 0;
            else if (pkt->isL1_Hit())         inst->regLd_Hit_Level = 1;
            else if (pkt->isL2_Hit())         inst->regLd_Hit_Level = 2;
            else if (pkt->isMem_Hit())        inst->regLd_Hit_Level = 3;
        }
        DPRINTF(JY, "reg load [sn:%lli] hits at level %d\n", inst->seqNum, inst->regLd_Hit_Level);
    }

    if (inst->isLoad() && inst->needPostFetch() && (pkt->isExpose() || pkt->isValidate())) { // pkt is expose/validate
        if (state->isSplit) {
            if (pkt->isFirst()) {
                if      (pkt->isL0_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 0);
                else if (pkt->isL1_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 1);
                else if (pkt->isL2_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 2);
                else if (pkt->isMem_Hit())    inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 3);
            }
            else {
                if      (pkt->isL0_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 0);
                else if (pkt->isL1_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 1);
                else if (pkt->isL2_Hit())     inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 2);
                else if (pkt->isMem_Hit())    inst->ExpVal_Hit_Level = max(inst->ExpVal_Hit_Level, 3);
            }
        }
        else {
            if      (pkt->isL0_Hit())         inst->ExpVal_Hit_Level = 0;
            else if (pkt->isL1_Hit())         inst->ExpVal_Hit_Level = 1;
            else if (pkt->isL2_Hit())         inst->ExpVal_Hit_Level = 2;
            else if (pkt->isMem_Hit())        inst->ExpVal_Hit_Level = 3;
        }
        if (pkt->isExpose())
            DPRINTF(JY, "expose of [sn:%lli] hits at level %d\n", inst->seqNum, inst->ExpVal_Hit_Level);
        else if (pkt->isValidate())
            DPRINTF(JY, "validate of [sn:%lli] hits at level %d\n", inst->seqNum, inst->ExpVal_Hit_Level);
        else
            assert(0);
    }

    if (state->cacheBlocked) {
        // This is the first half of a previous split load,
        // where the 2nd half blocked, ignore this response
        DPRINTF(IEW, "[sn:%lli]: Response from first half of earlier "
                "blocked split load recieved. Ignoring.\n", inst->seqNum);
        if (pkt->isFinalPacket)
            delete state;
        return;
    }

    // need to update hit info for corresponding instruction
    // Jiyong: in MLDOM, we don't do these
    if (!cpu->enableMLDOM) {
        if (pkt->isL0_Hit() && pkt->isSpec() && pkt->isRead()){
            if (state->isSplit && ! pkt->isFirst()){
                inst->setL1HitHigh();
            } else {
                inst->setL1HitLow();
            }
        } else if (!pkt->isSpec()) {
            setSpecBufState(pkt->req);
        }
    }
    else {
        assert(cpu->enableMLDOM);
        if (cpu->enableOblSContention && !pkt->isSpec())
            setSpecBufState(pkt->req);

        // Jiyong, MLDOM: special handle started
        if (pkt->isSpec() && pkt->isRead()) {   // a Spec load, and the confirmation packet
            // sanity check
            if (!pkt->isFinalPacket)
                assert(pkt->confirmPkt->isSpec());
            assert(inst->needPostFetch());
            if (state->isSplit) {
                // a split load
                if (pkt->isFirst()) {
                    // set the first packet flags
                    inst->oblS_FstHalfHit_SB  = pkt->isSB_Hit();
                    inst->oblS_FstHalfHit_L0  = pkt->isL0_Hit();
                    inst->oblS_FstHalfHit_L1  = pkt->isL1_Hit();
                    inst->oblS_FstHalfHit_L2  = pkt->isL2_Hit();
                    inst->oblS_FstHalfHit_Mem = pkt->isMem_Hit();
                    inst->oblS_FstHalfHit     = pkt->isSB_Hit() || pkt->isL0_Hit() || pkt->isL1_Hit() || pkt->isL2_Hit() || pkt->isMem_Hit();
                    if (pkt->isFinalPacket)
                        inst->oblS_FstHalfComplete = true;
                }
                else {
                    // set the second packet flags
                    inst->oblS_SndHalfHit_SB  = pkt->isSB_Hit();
                    inst->oblS_SndHalfHit_L0  = pkt->isL0_Hit();
                    inst->oblS_SndHalfHit_L1  = pkt->isL1_Hit();
                    inst->oblS_SndHalfHit_L2  = pkt->isL2_Hit();
                    inst->oblS_SndHalfHit_Mem = pkt->isMem_Hit();
                    inst->oblS_SndHalfHit     = pkt->isSB_Hit() || pkt->isL0_Hit() || pkt->isL1_Hit() || pkt->isL2_Hit() || pkt->isMem_Hit();
                    if (pkt->isFinalPacket)
                        inst->oblS_SndHalfComplete = true;
                }
                inst->oblS_Hit_SB  = inst->oblS_FstHalfHit_SB &&
                                     inst->oblS_SndHalfHit_SB;
                if (inst->oblS_Hit_SB)
                    assert(cpu->enableOblSContention);

                inst->oblS_Hit_L0  = !inst->oblS_Hit_SB &&
                                     (inst->oblS_FstHalfHit_SB || inst->oblS_FstHalfHit_L0) &&
                                     (inst->oblS_SndHalfHit_SB || inst->oblS_SndHalfHit_L0);

                inst->oblS_Hit_L1  = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 &&
                                     (inst->oblS_FstHalfHit_SB || inst->oblS_FstHalfHit_L0 || inst->oblS_FstHalfHit_L1) &&
                                     (inst->oblS_SndHalfHit_SB || inst->oblS_SndHalfHit_L0 || inst->oblS_SndHalfHit_L1);

                inst->oblS_Hit_L2  = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 && !inst->oblS_Hit_L1 &&
                                     (inst->oblS_FstHalfHit_SB || inst->oblS_FstHalfHit_L0 || inst->oblS_FstHalfHit_L1 || inst->oblS_FstHalfHit_L2) &&
                                     (inst->oblS_SndHalfHit_SB || inst->oblS_SndHalfHit_L0 || inst->oblS_SndHalfHit_L1 || inst->oblS_SndHalfHit_L2);

                inst->oblS_Hit_Mem = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 && !inst->oblS_Hit_L1 && !inst->oblS_Hit_L2 &&
                                     (inst->oblS_FstHalfHit_SB || inst->oblS_FstHalfHit_L0 || inst->oblS_FstHalfHit_L1 || inst->oblS_FstHalfHit_L2 || inst->oblS_FstHalfHit_Mem) &&
                                     (inst->oblS_SndHalfHit_SB || inst->oblS_SndHalfHit_L0 || inst->oblS_SndHalfHit_L1 || inst->oblS_SndHalfHit_L2 || inst->oblS_SndHalfHit_Mem);

                inst->oblS_Hit     = inst->oblS_Hit_SB || inst->oblS_Hit_L0 || inst->oblS_Hit_L1 || inst->oblS_Hit_L2 || inst->oblS_Hit_Mem;

                inst->oblS_Complete = inst->oblS_FstHalfComplete && inst->oblS_SndHalfComplete;

                DPRINTF(JY, "This SpecLD (split) has HitSB = %d. HitL0 = %d, HitL1 = %d, HitL2 = %d, HitL3 = %d, complete = %d\n",
                        inst->oblS_Hit_SB, inst->oblS_Hit_L0, inst->oblS_Hit_L1, inst->oblS_Hit_L2, inst->oblS_Hit_Mem, inst->oblS_Complete);
            }
            else {
                // not a split load
                inst->oblS_Hit_SB  = pkt->isSB_Hit();

                inst->oblS_Hit_L0  = !inst->oblS_Hit_SB &&
                                     pkt->isL0_Hit();

                inst->oblS_Hit_L1  = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 &&
                                     pkt->isL1_Hit();

                inst->oblS_Hit_L2  = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 && !inst->oblS_Hit_L1 &&
                                     pkt->isL2_Hit();

                inst->oblS_Hit_Mem = !inst->oblS_Hit_SB && !inst->oblS_Hit_L0 && !inst->oblS_Hit_L1 && !inst->oblS_Hit_L2 &&
                                     pkt->isMem_Hit();

                inst->oblS_Hit     = inst->oblS_Hit_SB || inst->oblS_Hit_L0 || inst->oblS_Hit_L1 || inst->oblS_Hit_L2 || inst->oblS_Hit_Mem;

                if (pkt->isFinalPacket)
                    inst->oblS_Complete = true;

                DPRINTF(JY, "This SpecLD (not split) now has HitSB = %d, HitL0 = %d, HitL1 = %d, HitL2 = %d, HitL3 = %d, complete = %d\n",
                        inst->oblS_Hit_SB, inst->oblS_Hit_L0, inst->oblS_Hit_L1, inst->oblS_Hit_L2, inst->oblS_Hit_Mem, inst->oblS_Complete);
            }
            // Jiyong, MLDOM, TODO: add the value prediction here
            // assume it's skipped for now
        }
    }

    // Jiyong: print latency numbers
    if (inst->isLoad() && !pkt->isSpec() && !pkt->isExpose() && !pkt->isValidate()) {
        inst->loadLatency = cpu->numCycles.value() - inst->issueCycle;
        DPRINTF(JY, "Writeback event for nonspec ld [sn:%lli] at cycle %lld of load from Level %d (%d, %d,%d,%d,%d) Latency = %d\n", inst->seqNum, cpu->numCycles.value(), inst->regLd_Hit_Level, 
                pkt->isSB_Hit(), pkt->isL0_Hit(), pkt->isL1_Hit(), pkt->isL2_Hit(), pkt->isMem_Hit(), inst->loadLatency);
        if (pkt->isL0_Hit()) {
            cpu->num_L0_nonspec_access ++;
            cpu->tot_L0_nonspec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isL1_Hit()) {
            cpu->num_L1_nonspec_access ++;
            cpu->tot_L1_nonspec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isL2_Hit()) {
            cpu->num_L2_nonspec_access ++;
            cpu->tot_L2_nonspec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isMem_Hit()) {
            cpu->num_Mem_nonspec_access ++;
            cpu->tot_Mem_nonspec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
    }
    else if (inst->isLoad() && pkt->isSpec()) {
        DPRINTF(JY, "Writeback event [sn:%lli] from level %d Latency = %d\n", inst->seqNum, pkt->fromLevel, cpu->numCycles.value() - inst->loadLatency);
        if (pkt->isL0_Hit() && pkt->fromLevel == 0) {
            cpu->num_L0_spec_access ++;
            cpu->tot_L0_spec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isL1_Hit() && pkt->fromLevel == 1) {
            cpu->num_L1_spec_access ++;
            cpu->tot_L1_spec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isL2_Hit() && pkt->fromLevel == 2) {
            cpu->num_L2_spec_access ++;
            cpu->tot_L2_spec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
        else if (pkt->isMem_Hit() && pkt->fromLevel == 3) {
            cpu->num_Mem_spec_access ++;
            cpu->tot_Mem_spec_access_latency += cpu->numCycles.value() - inst->issueCycle;
        }
    }

    // If this is a split access, wait until all packets are received.
    if (pkt->isFinalPacket) { // Jiyong, MLDOM: only final packet will check this; side packets will only copy data
        if (TheISA::HasUnalignedMemAcc && !state->complete()) {
            // Not the good place, but we need to fix the memory leakage
            if (pkt->isExpose() || pkt->isValidate()){
                DPRINTF(JY, "first packet is not served for inst [sn:%lli]\n", pkt->seqNum);
                assert(!inst->needDeletePostReq());
                assert(!pkt->isInvalidate());
                delete pkt->req;
            }
            return;
        }
    }

    // Jiyong, SDO: State machine logic
    assert(!cpu->switchedOut());
    if (!inst->isSquashed()) {
        if (!state->noWB) {
            // Only loads and store conditionals perform the writeback
            // after receving the response from the memory
            // [mengjia] validation also needs writeback, expose do not need
            assert((inst->isLoad() || inst->isStoreConditional()));

            // Jiyong, MLDOM: if we see an exposure/validation for this spec load, we skip the writeback
            if (cpu->enableMLDOM) {
                if (inst->isLoad() && inst->needPostFetch()) {  // this is a specload

                    bool earlyHit = pkt->isSpec() && !pkt->isFinalPacket && inst->oblS_Hit && pkt->carryData;       // a intermediate response, from a cache hit
                    bool confirm  = pkt->isSpec() &&  pkt->isFinalPacket && inst->oblS_Complete;  // the last response, may be hit or miss

                    DPRINTF(JY, "earlyHit = %d, confirm = %d\n", earlyHit, confirm);

                    // stats counting
                    if (confirm) {
                        if (inst->pred_level == Cache_L1) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdL0_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdL0_hitL0++;
                            else
                                numSpecLdL0_miss++;
                        }
                        else if (inst->pred_level == Cache_L2) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdL1_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdL1_hitL0++;
                            else if (inst->oblS_Hit_L1)
                                numSpecLdL1_hitL1++;
                            else
                                numSpecLdL1_miss++;
                        }
                        else if (inst->pred_level == Cache_L3) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdL2_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdL2_hitL0++;
                            else if (inst->oblS_Hit_L1)
                                numSpecLdL2_hitL1++;
                            else if (inst->oblS_Hit_L2)
                                numSpecLdL2_hitL2++;
                            else
                                numSpecLdL2_miss++;
                        }
                        else if (inst->pred_level == DRAM) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdMem_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdMem_hitL0++;
                            else if (inst->oblS_Hit_L1)
                                numSpecLdMem_hitL1++;
                            else if (inst->oblS_Hit_L2)
                                numSpecLdMem_hitL2++;
                            else if (inst->oblS_Hit_Mem)
                                numSpecLdMem_hitMem++;
                            else
                                numSpecLdMem_miss++;
                        }
                        else if (inst->pred_level == Perfect_Level) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdPerfect_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdPerfect_hitL0++;
                            else if (inst->oblS_Hit_L1)
                                numSpecLdPerfect_hitL1++;
                            else if (inst->oblS_Hit_L2)
                                numSpecLdPerfect_hitL2++;
                            else if (inst->oblS_Hit_Mem)
                                numSpecLdPerfect_hitMem++;
                            else
                                numSpecLdPerfect_miss++;
                        }
                        else if (inst->pred_level == PerfectUnsafe_Level) {
                            if (inst->oblS_Hit_SB)
                                numSpecLdPerfectUnsafe_hitSB++;
                            else if (inst->oblS_Hit_L0)
                                numSpecLdPerfectUnsafe_hitL0++;
                            else if (inst->oblS_Hit_L1)
                                numSpecLdPerfectUnsafe_hitL1++;
                            else if (inst->oblS_Hit_L2)
                                numSpecLdPerfectUnsafe_hitL2++;
                            else if (inst->oblS_Hit_Mem)
                                numSpecLdPerfectUnsafe_hitMem++;
                            else
                                numSpecLdPerfectUnsafe_miss++;
                        }
                    }

                    //if (confirm && !inst->oblS_Hit)
                        //printf("WARNING: no hit for spec load [sn:%ld] when confirm\n", inst->seqNum);

                    // (for spec load) check the state machine transition
                    if (pkt->isSpec() && !earlyHit && !confirm) {
                        assert(!pkt->isExpose());
                        assert(!pkt->isValidate());
                        DPRINTF(JY, "load [sn:%lli] receive an intermediate packet which is not a hit. No action should be taken\n", inst->seqNum);
                    }
                    // BASIC scheme begins: when the load only sends 1 response
                    else if (inst->MLDOM_state == MLDOM_STATE_A && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_AB;
                        // ld sent OblS ++ OblS returns
                        // Actions: hit: writeback returned data of OblS
                        //          miss: writeback predicted data (currently not implemented)
                        if (inst->fault == NoFault) {   // external eviction will set expose to done
                            assert (!inst->isExposeSent());
                            assert (!inst->isExposeCompleted());
                        }

                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            // dual packet
                            assert(inst->oblS_Complete);
                            assert(!inst->doneWB);
                            writeback(inst, state->mainPkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                            if (inst->oblS_Hit) {
                                assert(!inst->valPred_onMiss);
                                DPRINTF(JY, "A+B: load [sn:%lli] has oblS returning hit and validate not sent (dual packet).\n", inst->seqNum);
                            }
                            else {
                                num_ABmiss++;
                                inst->valPred_onMiss = true;
                                DPRINTF(JY, "A+B: load [sn:%lli] has oblS returning miss (value predict result, pending squash) and validate not sent (dual packet).\n", inst->seqNum);
                            }
                        } else {
                            // single packet
                            assert(inst->oblS_Complete);
                            assert(!inst->doneWB);
                            writeback(inst, pkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                            if (inst->oblS_Hit) {
                                assert(!inst->valPred_onMiss);
                                DPRINTF(JY, "A+B: load [sn:%lli] has oblS returning hit and validate not sent (single packet);.\n", inst->seqNum);
                            }
                            else {
                                num_ABmiss++;
                                inst->valPred_onMiss = true;
                                DPRINTF(JY, "A+B: load [sn:%lli] has oblS returning miss (value predict result, pending squash) and validate not sent (single packet);.\n", inst->seqNum);
                            }
                        }
                    }
                    // AB -> ABC is in ExposeLoads()
                    else if (inst->MLDOM_state == MLDOM_STATE_ABC && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_ABCD;
                        // ld sent OblS, OblS returned, ld sent Val ++ Val returns
                        // Actions: no action here. Call CompleteValidate later
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            assert (inst->oblS_Complete);
                            assert (inst->doneWB && inst->doneWB_byOblS);
                            DPRINTF(JY, "ABC+D: Validate for load [sn:%lli] see the load (2 packet) completed\n", inst->seqNum);
                        }
                        else {
                            assert (inst->oblS_Complete);
                            assert (inst->doneWB && inst->doneWB_byOblS);
                            DPRINTF(JY, "ABC+D: Validate for load [sn:%lli] see the load (1 packet) completed\n", inst->seqNum);
                        }
                        // completeValidate() will validate the result

                        // update the location prediction if OblS was miss, because if it's a hit, we would update it when issuing Exp/Val
                        if (!inst->oblS_Hit)
                            updateLocationPredictor(inst, 2);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ABC && pkt->isExpose()) {
                        inst->MLDOM_state = MLDOM_STATE_ABCD;
                        // ld send OblS, OblS returned, ld sent Exp ++ Exp returns
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            assert (inst->oblS_Complete);
                            assert (inst->doneWB && inst->doneWB_byOblS);
                            DPRINTF(JY, "ABC+D: Validate for load [sn:%lli] see the load (2 packet) completed\n", inst->seqNum);
                        }
                        else {
                            assert (inst->oblS_Complete);
                            assert (inst->doneWB && inst->doneWB_byOblS);
                            DPRINTF(JY, "ABC+D: Validate for load [sn:%lli] see the load (1 packet) completed\n", inst->seqNum);
                        }
                        // completeValidate() will expose the result

                        // update the location prediction if OblS was miss, because if it's a hit, we would update it when issuing Exp/Val
                        if (!inst->oblS_Hit)
                            updateLocationPredictor(inst, 2);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AC && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_ACB;
                        // ld sent OblS; ld sent Val ++ OblS returns
                        // Actions: hit: writeback returned data
                        //          miss: do nothing
                        //assert (inst->readyToExpose() && !inst->isExposeCompleted());
                        if (inst->fault == NoFault) { // external invalidation can set expose to done
                            if (cpu->pred_type != Perfect)
                                assert(inst->readyToExpose() && !inst->isExposeCompleted());  // non-perfect must haven't completed validation
                            else if (dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe())
                                assert(inst->readyToExpose() && !inst->isExposeCompleted());  // perfect-safe must haven't completed validation
                            // perfect-unsafe never does validation (it only needs exposure)
                        }

                        assert (inst->oblS_Complete);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            if (inst->oblS_Hit) {
                                assert(!inst->doneWB);
                                writeback(inst, state->mainPkt);
                                inst->doneWB = true;
                                inst->doneWB_byOblS = true;
                                DPRINTF(JY, "AC+B: load [sn:%lli] has oblS returns and validate sent(not returned). Two packets hit\n", inst->seqNum);
                                // also update the location predictor based on return of OblS
                                updateLocationPredictor(inst, 1);
                            }
                            else {
                                // do nothing if OblS miss
                                DPRINTF(JY, "AC+B: load [sn:%lli] has oblS returns and validate sent(not returned). At lease one packet miss\n", inst->seqNum);

                                // we delay the predictor update to ACBD, when validation result returns
                            }
                        }
                        else {
                            if (inst->oblS_Hit) {
                                assert(!inst->doneWB);
                                writeback(inst, pkt);
                                inst->doneWB = true;
                                inst->doneWB_byOblS = true;
                                DPRINTF(JY, "AC+B: load [sn:%lli] has oblS returns and validate sent(not returned). One packet hits\n", inst->seqNum);
                                // also update the location predictor based on return of OblS
                                updateLocationPredictor(inst, 1);
                            }
                            else {
                                // do nothing if OblS miss
                                DPRINTF(JY, "AC+B: load [sn:%lli] has oblS returns and validate sent(not returned). The packet misses\n", inst->seqNum);

                                // we delay the predictor update to ACBD, when validation result returns
                            }
                        }
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AC && pkt->isExpose()) {  // only appear in Perfect-Unsafe (no validation)
                        // ld sent obls; ld sent exp; exp returns
                        // NOTE: this is impossible unless perdictor is Perfect-Unsafe
                        //       which guarantees 100% hit rate && single-threaded thus eliminating val
                        assert (cpu->pred_type == Perfect); // perfect
                        assert (!dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe()); // AND unsafe
                        // no action required
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACB && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_ACBD;
                        // ld sent OblS; ld sent Val; OblS returned ++ Val returns
                        // Actions: OblS was a hit: do nothing, call completeValidate later
                        //          OblS was a miss: writeback return data of Validate
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            assert(inst->oblS_Complete);
                            if (inst->oblS_Hit) {
                                // do nothing here, validate the OblS returns on completeValidate
                                assert(inst->doneWB && inst->doneWB_byOblS);
                                DPRINTF(JY, "ACB+D: Validate for load [sn:%lli] see the load(2 packet) returned hit\n", inst->seqNum);
                            }
                            else {
                                // Jiyong, MLDOM: if it was OblS miss, we writeback the result of Validate here
                                writeback(inst, state->mainPkt);
                                inst->doneWB = true;
                                inst->doneWB_byOblS = false;
                                DPRINTF(JY, "ACB+D: Validate for load [sn:%lli] see the load(2 packet) returned miss\n", inst->seqNum);
                                // update the location predictor based on result of validate, since oblS was a miss
                                updateLocationPredictor(inst, 2);
                            }
                        }
                        else {
                            assert(inst->oblS_Complete);
                            if (inst->oblS_Hit) {
                                // do nothing here, validate the OblS returns on completeValidate
                                assert(inst->doneWB && inst->doneWB_byOblS);
                                DPRINTF(JY, "ACB+D: Validate for load [sn:%lli] see the load(1 packet) returned hit\n", inst->seqNum);
                            }
                            else {
                                // Jiyong, MLDOM: if it was OblS miss, we writeback the result of Validate here
                                writeback(inst, pkt);
                                inst->doneWB = true;
                                inst->doneWB_byOblS = false;
                                DPRINTF(JY, "ACB+D: Validate for load [sn:%lli] see the load(1 packet) returned miss\n", inst->seqNum);
                                // update the location predictor based on result of validate, since oblS was a miss
                                updateLocationPredictor(inst, 2);
                            }
                        }
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACB && pkt->isExpose()) { // only appear in Perfect-Unsafe (no validation)
                        // ld sent obls; ld sent exp; exp returns
                        // NOTE: this is impossible unless perdictor is Perfect-Unsafe
                        //       which guarantees 100% hit rate && single-threaded thus eliminating val
                        assert (cpu->pred_type == Perfect); // perfect
                        assert (!dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe()); // AND unsafe
                        // no action required
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AC && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_ACD;
                        // ld sent OblS; ld sent Val ++ Val returns
                        // Actions: writeback return data of Val
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            assert(!inst->oblS_Complete);
                            assert(!inst->doneWB);
                            writeback(inst, state->mainPkt); // Jiyong, MLDOM: we writeback the result of validate
                            inst->doneWB = true;
                            inst->doneWB_byOblS = false;
                            DPRINTF(JY, "AC+D: Validate for load [sn:%lli] see the OblS not returning\n", inst->seqNum);
                        }
                        else {
                            assert(!inst->oblS_Complete);
                            assert(!inst->doneWB);
                            writeback(inst, pkt); // Jiyong, MLDOM: we writeback the result of validate
                            inst->doneWB = true;
                            inst->doneWB_byOblS = false;
                            DPRINTF(JY, "AC+D: Validate for load [sn:%lli] see the OblS not returning\n", inst->seqNum);
                        }

                        // update the predictor using return of validation (as oblS result will be dismissed)
                        updateLocationPredictor(inst, 2);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACD && earlyHit) {
                        // ld sent OblS; ld sent Val; Val returned ++ OblS return early hit
                        // Actions: do nothing
                        assert(inst->isExposeCompleted());
                        assert(inst->doneWB && !inst->doneWB_byOblS); // validate has done the writeback
                        DPRINTF(JY, "ACD+Nothing: load [sn:%lli] has oblS returns early hit but expose/validate has finished\n", inst->seqNum);
                        // predictor is updated in the case above
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACD && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_ACDB;
                        // ld sent OblS; ld sent Val; Val returned ++ OblS returned
                        // Actions: do nothing
                        assert(inst->isExposeCompleted());
                        assert(inst->doneWB && !inst->doneWB_byOblS); // validate has done the writeback
                        DPRINTF(JY, "ACD+B: load [sn:%lli] has oblS returns but expose/validate has finished\n", inst->seqNum);
                        // predictor is updated in the case above
                    }
                    // BASIC scheme ends
                    //
                    // Advanced scheme: when load sends multiple response
                    else if (inst->MLDOM_state == MLDOM_STATE_A && earlyHit) {
                        assert(inst->oblS_Hit);
                        inst->MLDOM_state = MLDOM_STATE_Ab;
                        num_Ab++;
                        // ld sent OblS ++ a early hit returned
                        // Action: we cannot writeback the result now. Write back when the ld becomes safe
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            inst->pkt_delayed_wb = state->mainPkt;
                            DPRINTF(JY, "A+b: load [sn:%lli] has oblS returns a early hit (2 packets) when the load is still unsafe\n", inst->seqNum);
                            PacketPtr pkt_ptr = state->mainPkt;
                            //uint8_t* _data = new uint8_t [pkt_ptr->getSize()];
                            //memcpy(_data, pkt_ptr->getPtr<uint8_t>(), pkt_ptr->getSize());
                            for (int i = 0; i < pkt_ptr->getSize(); i++) {
                                DPRINTF(JY, "   delay data [sn:%lli,%lli]: %d: %d\n", inst->seqNum, pkt_ptr->seqNum, i, pkt_ptr->getPtr<uint8_t>()[i]);
                            }
                        }
                        else {
                            inst->pkt_delayed_wb = pkt->confirmPkt;
                            DPRINTF(JY, "A+b: load [sn:%lli] has oblS returns a early hit (1 packets) when the load is still unsafe\n", inst->seqNum);
                            PacketPtr pkt_ptr = pkt->confirmPkt;
                            //uint8_t* _data = new uint8_t [pkt_ptr->getSize()];
                            //memcpy(_data, pkt_ptr->getPtr<uint8_t>(), pkt_ptr->getSize());
                            for (int i = 0; i < pkt_ptr->getSize(); i++) {
                                DPRINTF(JY, "   delay data [sn:%lli,%lli]: %d: %d\n", inst->seqNum, pkt_ptr->seqNum, i, pkt_ptr->getPtr<uint8_t>()[i]);
                            }
                        }
                        // cannot update predictor now since it's unsafe
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_Ab && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_AB;
                        // ld sent oblS, oblS return an earlyHit ++ oblS return complete
                        // Actions: it was a hit, so writeback the delayed packet
                        assert(inst->oblS_Hit);
                        assert(inst->pkt_delayed_wb);
                        assert(!inst->doneWB);

                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            // dual packet
                            DPRINTF(JY, "Ab+B: load [sn:%lli] has oblS returned a early hit now a complete confirmation arrives (2 packets) (load is still unsafe)\n", inst->seqNum);
                            writeback(inst, state->mainPkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                        } else {
                            // single packet
                            DPRINTF(JY, "Ab+B: load [sn:%lli] has oblS returned a early hit now a complete confirmation arrives (1 packets) (load is still unsafe)\n", inst->seqNum);
                            writeback(inst, pkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                        }
                        // cannot update predictor now since it's unsafe
                        DPRINTF(JY, "Cannot update predictor right now as load [sn:%lli] is still unsafe\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbC && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_AbCB;
                        // no need to do anything
                        assert (inst->oblS_Hit);
                        assert (inst->oblS_Complete);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbCB && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_AbCBD;
                        // ld sent OblS; OblS early hit returned; ld sent Val(writeback early return); OblS confirm returned ++ Val returns
                        // Action:  no action here. Call completeValidate later to check if results match
                        assert (inst->oblS_Hit);
                        assert (inst->oblS_Complete);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit)
                            DPRINTF(JY, "AbCB+D: Validate for load [sn:%lli] see the load (2 packet) returned hit\n", inst->seqNum);
                        else
                            DPRINTF(JY, "AbCB+D: Validate for load [sn:%lli] see the load (1 packet) returned hit\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbCB && pkt->isExpose()) {
                        inst->MLDOM_state = MLDOM_STATE_AbCBD;
                        // ld sent OblS; OblS early hit returned; ld sent Exp(writeback early return); OblS confirm returned ++ Exp returns
                        // Action:  no action here.
                        assert (inst->oblS_Hit);
                        assert (inst->oblS_Complete);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit)
                            DPRINTF(JY, "AbCB+D: Expose for load [sn:%lli] see the load (2 packet) returned hit\n", inst->seqNum);
                        else
                            DPRINTF(JY, "AbCB+D: Expose for load [sn:%lli] see the load (1 packet) returned hit\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbC && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_AbCD;
                        // ld sent OblS; OblS early hit returned; ld sent Val(writeback early return) ++ Val returns
                        // Action:  no action here. Call completeValidate later to check if results match
                        assert (inst->oblS_Hit);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit)
                            DPRINTF(JY, "AbCB+D: Validate for load [sn:%lli] see the load (2 packet) returned hit\n", inst->seqNum);
                        else
                            DPRINTF(JY, "AbCB+D: Validate for load [sn:%lli] see the load (1 packet) returned hit\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbC && pkt->isExpose()) {
                        inst->MLDOM_state = MLDOM_STATE_AbCD;
                        // ld sent OblS; OblS early hit returned; ld sent Exp(writeback early return) ++ Exp returns
                        // Action:  no action here.
                        assert (inst->oblS_Hit);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit)
                            DPRINTF(JY, "AbCB+D: Expose for load [sn:%lli] see the load (2 packet) returned hit\n", inst->seqNum);
                        else
                            DPRINTF(JY, "AbCB+D: Expose for load [sn:%lli] see the load (1 packet) returned hit\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AbCD && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_AbCDB;
                        // no need to do anything
                        assert (inst->oblS_Hit);
                        assert (inst->oblS_Complete);
                        assert (inst->isExposeCompleted());
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_AC && earlyHit) {
                        inst->MLDOM_state = MLDOM_STATE_ACb;
                        // ld sent OblS; ld sent Val ++ OblS earlyhit returns;
                        // Action:  writeback returned data
                        if (inst->fault == NoFault) { // external invalidation can set expose to done
                            if (cpu->pred_type != Perfect)
                                assert(inst->readyToExpose() && !inst->isExposeCompleted());  // non-perfect must haven't completed validation
                            else if (dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe())
                                assert(inst->readyToExpose() && !inst->isExposeCompleted());  // perfect-safe must haven't completed validation
                            // perfect-unsafe never does validation (it only needs exposure)
                        }

                        assert (!inst->oblS_Complete);
                        assert (inst->oblS_Hit);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit) {
                            assert(!inst->doneWB);
                            writeback(inst, state->mainPkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                            DPRINTF(JY, "AC+b: load [sn:%lli] has oblS returns and validate sent(not returned). Two packets hit\n", inst->seqNum);
                            // update the result based on return of oblS early hit
                            updateLocationPredictor(inst, 1);
                        }
                        else {
                            assert(!inst->doneWB);
                            writeback(inst, pkt);
                            inst->doneWB = true;
                            inst->doneWB_byOblS = true;
                            DPRINTF(JY, "AC+b: load [sn:%lli] has oblS returns and validate sent(not returned). One packet hits\n", inst->seqNum);
                            // update the result based on return of oblS early hit
                            updateLocationPredictor(inst, 1);
                        }
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACb && confirm) {
                        // only need to merge the state. No further action
                        inst->MLDOM_state = MLDOM_STATE_ACB;
                        DPRINTF(JY, "ACb+B: load [sn:%lli] has validate sent then earlyHit returns then OblS confirm returns. Go to ACB\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACb && pkt->isValidate()) {
                        inst->MLDOM_state = MLDOM_STATE_ACbD;
                        // ld sent OblS; ld sent Val; OblS earlyhit returns ++ Val returns
                        // Action: no action here. Call completeValidate later to check if results match
                        assert (inst->oblS_Hit);
                        assert (!inst->oblS_Complete);
                        assert (inst->doneWB && inst->doneWB_byOblS);
                        if (TheISA::HasUnalignedMemAcc && state->isSplit)
                            DPRINTF(JY, "ACb+D: Validate for load [sn:%lli] see the load (2 packet) returned hit\n", inst->seqNum);
                        else
                            DPRINTF(JY, "ACb+D: Validate for load [sn:%lli] see the load (1 packet) returned hit\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_ACbD && confirm) {
                        inst->MLDOM_state = MLDOM_STATE_ACbDB;
                        // no need to do anything
                        assert (inst->oblS_Hit);
                        assert (inst->oblS_Complete);
                        assert (inst->isExposeCompleted());
                        DPRINTF(JY, "ACbD+B: load [sn:%lli] has OblS confirm returns. Go to ACbDB\n", inst->seqNum);
                    }
                    else if (inst->MLDOM_state == MLDOM_STATE_toBeSquash) {
                        assert (!inst->obls_TLB_Hit);
                        DPRINTF(JY, "toBESquash: load [sn:%lli] was obls TLB miss and is ignored.\n", inst->seqNum);
                    }
                    else {
                        printf("ERROR: unknown MLDOM state transition for load [sn:%lu] (current state = %d) isVal=%d, isExp=%d.\n", inst->seqNum, inst->MLDOM_state, pkt->isValidate(), pkt->isExpose());
                        assert(0);
                    }
                } // if (is obls load)
                else { // normal load
                    DPRINTF(JY, "a regular load [sn:%lli] calls writeback()\n", inst->seqNum);
                    assert(!pkt->isValidate() && !pkt->isExpose()); // expose/validate should not do writeback here
                    // (a normal load or store-conditional) we do the writeback
                    if (!TheISA::HasUnalignedMemAcc || !state->isSplit || !state->isLoad) { // not a split packet
                        writeback(inst, pkt);
                    } else {
                        writeback(inst, state->mainPkt);
                    }
                    // update location predictor based on the return of non-spec load
                    if (inst->isLoad())
                        updateLocationPredictor(inst, 0);
                }
            } // if (cpu->enableMLDOM) 
            else {
                assert(!cpu->enableMLDOM);
                // general case
                if (!TheISA::HasUnalignedMemAcc || !state->isSplit || !state->isLoad) { // not a split packet
                    writeback(inst, pkt);
                } else {
                    writeback(inst, state->mainPkt);
                }
            }
        }

        if (inst->isStore()) {
            completeStore(state->idx);
        }

        if (pkt->isValidate() || pkt->isExpose()) {
            completeValidate(inst, pkt);
        }
    }

    if (pkt->isFinalPacket) { // Jiyong, MLDOM: all intermediate packets should not delete these (state and request)
        if (TheISA::HasUnalignedMemAcc && state->isSplit && state->isLoad) {
            delete state->mainPkt->req;
            delete state->mainPkt;
        }

        pkt->req->setAccessLatency();
        // probe point, not sure about the mechanism [mengjia]
        cpu->ppDataAccessComplete->notify(std::make_pair(inst, pkt));

        // Not the good place, but we need to fix the memory leakage
        if (pkt->isExpose() || pkt->isValidate()){
            assert(!inst->needDeletePostReq());
            assert(!pkt->isInvalidate());
            delete pkt->req;
        }
        delete state;
    }
}

template <class Impl>
LSQUnit<Impl>::LSQUnit()
    : loads(0), loadsToVLD(0), stores(0), storesToWB(0), cacheBlockMask(0), stalled(false),
      isStoreBlocked(false), isValidationBlocked(false), storeInFlight(false), hasPendingPkt(false),
      pendingPkt(nullptr)
{
}

template<class Impl>
void
LSQUnit<Impl>::init(O3CPU *cpu_ptr, IEW *iew_ptr, DerivO3CPUParams *params,
        LSQ *lsq_ptr, unsigned maxLQEntries, unsigned maxSQEntries,
        unsigned id)
{
    cpu = cpu_ptr;
    iewStage = iew_ptr;

    lsq = lsq_ptr;

    lsqID = id;

    DPRINTF(LSQUnit, "Creating LSQUnit%i object.\n",id);

    // Add 1 for the sentinel entry (they are circular queues).
    LQEntries = maxLQEntries + 1;
    SQEntries = maxSQEntries + 1;

    //Due to uint8_t index in LSQSenderState
    assert(LQEntries <= 256);
    assert(SQEntries <= 256);

    loadQueue.resize(LQEntries);
    storeQueue.resize(SQEntries);

    depCheckShift = params->LSQDepCheckShift;
    checkLoads = params->LSQCheckLoads;
    cacheStorePorts = params->cacheStorePorts;


    resetState();
}


template<class Impl>
void
LSQUnit<Impl>::resetState()
{
    loads = stores = loadsToVLD = storesToWB = 0;

    loadHead = loadTail = 0;

    storeHead = storeWBIdx = storeTail = 0;

    usedStorePorts = 0;

    retryPkt = NULL;
    memDepViolator = NULL;

    // Jiyong, MLDOM
    oblsMissInst = NULL;

    stalled = false;

    cacheBlockMask = ~(cpu->cacheLineSize() - 1);
}

template<class Impl>
std::string
LSQUnit<Impl>::name() const
{
    if (Impl::MaxThreads == 1) {
        return iewStage->name() + ".lsq";
    } else {
        return iewStage->name() + ".lsq.thread" + std::to_string(lsqID);
    }
}

template<class Impl>
void
LSQUnit<Impl>::regStats()
{
    lsqForwLoads
        .name(name() + ".forwLoads")
        .desc("Number of loads that had data forwarded from stores");

    taintedlsqForwLoads
        .name(name() + ".taintedforwLoads")
        .desc("Number of tainted loads that had data forwarded from stores");

    invAddrLoads
        .name(name() + ".invAddrLoads")
        .desc("Number of loads ignored due to an invalid address");

    lsqSquashedLoads
        .name(name() + ".squashedLoads")
        .desc("Number of loads squashed");

    lsqIgnoredResponses
        .name(name() + ".ignoredResponses")
        .desc("Number of memory responses ignored because the instruction is squashed");

    lsqMemOrderViolation
        .name(name() + ".memOrderViolation")
        .desc("Number of memory ordering violations");

    lsqSquashedStores
        .name(name() + ".squashedStores")
        .desc("Number of stores squashed");

    invAddrSwpfs
        .name(name() + ".invAddrSwpfs")
        .desc("Number of software prefetches ignored due to an invalid address");

    lsqBlockedLoads
        .name(name() + ".blockedLoads")
        .desc("Number of blocked loads due to partial load-store forwarding");

    lsqRescheduledLoads
        .name(name() + ".rescheduledLoads")
        .desc("Number of loads that were rescheduled");

    lsqCacheBlocked
        .name(name() + ".cacheBlocked")
        .desc("Number of times an access to memory failed due to the cache being blocked");

    lsqCacheBlockedByExpVal
        .name(name() + ".lsqCacheBlockedByExpVal")
        .desc("Number of times an exp/val to memory failed due to the cache being blocked");

    specBufHits
        .name(name() + ".specBufHits")
        .desc("Number of times an access hits in speculative buffer");

    specBufMisses
        .name(name() + ".specBufMisses")
        .desc("Number of times an access misses in speculative buffer");

    numValidates
        .name(name() + ".numValidates")
        .desc("Number of validates sent to cache");

    numExposes
        .name(name() + ".numExposes")
        .desc("Number of exposes sent to cache");

    numConvertedExposes
        .name(name() + ".numConvertedExposes")
        .desc("Number of exposes converted from validation");

    removedExposes
        .name(name() + ".removedExposes")
        .desc("Number of exposes removed on obls hit (disable_2ndld is set) [MLDOM]"); // Jiyong, MLDOM

    taintedSnoopLoadViolation
        .name(name() + ".taintedSnoopLoadViolation")
        .desc("Number of tainted loads that is squashed by cache invalidation [STT]");

    /* Jiyong, MLDOM */
    numNonSpecLdSent
        .name(name() + ".numNonSpecLdSent")
        .desc("Number of non-specload sent out to the memory subsystem [MLDOM]");

    numSpecLdL0Sent
        .name(name() + ".numSpecLdL0Sent")
        .desc("Number of specload L0 sent out to the memory subsystem [MLDOM]");

    numSpecLdL1Sent
        .name(name() + ".numSpecLdL1Sent")
        .desc("Number of specload L1 sent out to the memory subsystem [MLDOM]");

    numSpecLdL2Sent
        .name(name() + ".numSpecLdL2Sent")
        .desc("Number of specload L2 sent out to the memory subsystem [MLDOM]");

    numSpecLdMemSent
        .name(name() + ".numSpecLdMemSent")
        .desc("Number of specload Mem sent out to the memory subsystem [MLDOM]");

    numSpecLdPerfectSent
        .name(name() + ".numSpecLdPerfectSent")
        .desc("Number of specload perfect sent out to the memory subsystem [MLDOM]");

    numSpecLdPerfectUnsafeSent
        .name(name() + ".numSpecLdPerfectUnsafeSent")
        .desc("Number of specload perfect unsafe sent out to the memory subsystem [MLDOM]");

    numSquashOnMiss_SpecLdL0
        .name(name() + ".numSquashOnMiss_SpecLdL0")
        .desc("SpecLdL0 has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnMiss_SpecLdL1
        .name(name() + ".numSquashOnMiss_SpecLdL1")
        .desc("SpecLdL1 has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnMiss_SpecLdL2
        .name(name() + ".numSquashOnMiss_SpecLdL2")
        .desc("SpecLdL2 has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnMiss_SpecLdMem
        .name(name() + ".numSquashOnMiss_SpecLdMem")
        .desc("SpecLdMem has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnMiss_SpecLdPerfect
        .name(name() + ".numSquashOnMiss_SpecLdPerfect")
        .desc("SpecLdPerfect has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnMiss_SpecLdPerfectUnsafe
        .name(name() + ".numSquashOnMiss_SpecLdPerfectUnsafe")
        .desc("SpecLdPerfectUnsafe has miss, and later it triggers a squash when ld turns safe (mark inst as ReExec in Exposeload()) [MLDOM]");

    numSquashOnSpecTLBMiss
        .name(name() + ".numSquashOnSpecTLBMiss")
        .desc("SDO TLB Miss will proceed with incorrect paddr. Number of squashes on this [MLDOM]");

    numValidationFails
        .name(name() + ".numValidationFails")
        .desc("number of times validation results don't match oblS result [MLDOM]");

    numSpecLdL0_hitSB
        .name(name() + ".numSpecLdL0_hitSB")
        .desc("number of issued obls_L0 which hits Specbuffer [MLDOM]");
    numSpecLdL0_hitL0
        .name(name() + ".numSpecLdL0_hitL0")
        .desc("number of issued obls_L0 which hits L0 [MLDOM]");
    numSpecLdL0_miss
        .name(name() + ".numSpecLdL0_miss")
        .desc("number of issued obls_L0 which misses [MLDOM]");

    numSpecLdL1_hitSB
        .name(name() + ".numSpecLdL1_hitSB")
        .desc("number of issued obls_L1 which hits Specbuffer [MLDOM]");
    numSpecLdL1_hitL0
        .name(name() + ".numSpecLdL1_hitL0")
        .desc("number of issued obls_L1 which hits L0 [MLDOM]");
    numSpecLdL1_hitL1
        .name(name() + ".numSpecLdL1_hitL1")
        .desc("number of issued obls_L1 which hits L1 [MLDOM]");
    numSpecLdL1_miss
        .name(name() + ".numSpecLdL1_miss")
        .desc("number of issued obls_L1 which misses [MLDOM]");

    numSpecLdL2_hitSB
        .name(name() + ".numSpecLdL2_hitSB")
        .desc("number of issued obls_L2 which hits Specbuffer [MLDOM]");
    numSpecLdL2_hitL0
        .name(name() + ".numSpecLdL2_hitL0")
        .desc("number of issued obls_L2 which hits L0 [MLDOM]");
    numSpecLdL2_hitL1
        .name(name() + ".numSpecLdL2_hitL1")
        .desc("number of issued obls_L2 which hits L1 [MLDOM]");
    numSpecLdL2_hitL2
        .name(name() + ".numSpecLdL2_hitL2")
        .desc("number of issued obls_L2 which hits L2 [MLDOM]");
    numSpecLdL2_miss
        .name(name() + ".numSpecLdL2_miss")
        .desc("number of issued obls_L2 which misses [MLDOM]");

    numSpecLdMem_hitSB
        .name(name() + ".numSpecLdMem_hitSB")
        .desc("number of issued obls_Mem which hits Specbuffer [MLDOM]");
    numSpecLdMem_hitL0
        .name(name() + ".numSpecLdMem_hitL0")
        .desc("number of issued obls_Mem which hits L0 [MLDOM]");
    numSpecLdMem_hitL1
        .name(name() + ".numSpecLdMem_hitL1")
        .desc("number of issued obls_Mem which hits L1 [MLDOM]");
    numSpecLdMem_hitL2
        .name(name() + ".numSpecLdMem_hitL2")
        .desc("number of issued obls_Mem which hits L2 [MLDOM]");
    numSpecLdMem_hitMem
        .name(name() + ".numSpecLdMem_hitMem")
        .desc("number of issued obls_Mem which hits Mem [MLDOM]");
    numSpecLdMem_miss
        .name(name() + ".numSpecLdMem_miss")
        .desc("number of issued obls_Mem which misses [MLDOM]");

    numSpecLdPerfect_hitSB
        .name(name() + ".numSpecLdPerfect_hitSB")
        .desc("number of issued obls_Perfect which hits Specbuffer [MLDOM]");
    numSpecLdPerfect_hitL0
        .name(name() + ".numSpecLdPerfect_hitL0")
        .desc("number of issued obls_Perfect which hits L0 [MLDOM]");
    numSpecLdPerfect_hitL1
        .name(name() + ".numSpecLdPerfect_hitL1")
        .desc("number of issued obls_Perfect which hits L1 [MLDOM]");
    numSpecLdPerfect_hitL2
        .name(name() + ".numSpecLdPerfect_hitL2")
        .desc("number of issued obls_Perfect which hits L2 [MLDOM]");
    numSpecLdPerfect_hitMem
        .name(name() + ".numSpecLdPerfect_hitMem")
        .desc("number of issued obls_Perfect which hits Mem [MLDOM]");
    numSpecLdPerfect_miss
        .name(name() + ".numSpecLdPerfect_miss")
        .desc("number of issued obls_Perfect which misses [MLDOM]");

    numSpecLdPerfectUnsafe_hitSB
        .name(name() + ".numSpecLdPerfectUnsafe_hitSB")
        .desc("number of issued obls_PerfectUnsafe which hits Specbuffer [MLDOM]");
    numSpecLdPerfectUnsafe_hitL0
        .name(name() + ".numSpecLdPerfectUnsafe_hitL0")
        .desc("number of issued obls_PerfectUnsafe which hits L0 [MLDOM]");
    numSpecLdPerfectUnsafe_hitL1
        .name(name() + ".numSpecLdPerfectUnsafe_hitL1")
        .desc("number of issued obls_PerfectUnsafe which hits L1 [MLDOM]");
    numSpecLdPerfectUnsafe_hitL2
        .name(name() + ".numSpecLdPerfectUnsafe_hitL2")
        .desc("number of issued obls_PerfectUnsafe which hits L2 [MLDOM]");
    numSpecLdPerfectUnsafe_hitMem
        .name(name() + ".numSpecLdPerfectUnsafe_hitMem")
        .desc("number of issued obls_PerfectUnsafe which hits Mem [MLDOM]");
    numSpecLdPerfectUnsafe_miss
        .name(name() + ".numSpecLdPerfectUnsafe_miss")
        .desc("number of issued obls_PerfectUnsafe which misses [MLDOM]");

    num_ABmiss
        .name(name() + ".num_ABmiss")
        .desc("number of times an obls has AB state with miss, so will later be marked as pendingSquash [MLDOM]");

    num_Ab
        .name(name() + ".num_Ab")
        .desc("number of times an obls enters Ab state, so the prediction is not good enough [MLDOM]");
}

template<class Impl>
void
LSQUnit<Impl>::setDcachePort(MasterPort *dcache_port)
{
    dcachePort = dcache_port;
}

template<class Impl>
void
LSQUnit<Impl>::clearLQ()
{
    loadQueue.clear();
}

template<class Impl>
void
LSQUnit<Impl>::clearSQ()
{
    storeQueue.clear();
}

template<class Impl>
void
LSQUnit<Impl>::drainSanityCheck() const
{
    for (int i = 0; i < loadQueue.size(); ++i)
        assert(!loadQueue[i]);

    assert(storesToWB == 0);
    assert(loadsToVLD == 0);
    assert(!retryPkt);
}

template<class Impl>
void
LSQUnit<Impl>::takeOverFrom()
{
    resetState();
}

template<class Impl>
void
LSQUnit<Impl>::resizeLQ(unsigned size)
{
    unsigned size_plus_sentinel = size + 1;
    assert(size_plus_sentinel >= LQEntries);

    if (size_plus_sentinel > LQEntries) {
        while (size_plus_sentinel > loadQueue.size()) {
            DynInstPtr dummy;
            loadQueue.push_back(dummy);
            LQEntries++;
        }
    } else {
        LQEntries = size_plus_sentinel;
    }

    assert(LQEntries <= 256);
}

template<class Impl>
void
LSQUnit<Impl>::resizeSQ(unsigned size)
{
    unsigned size_plus_sentinel = size + 1;
    if (size_plus_sentinel > SQEntries) {
        while (size_plus_sentinel > storeQueue.size()) {
            SQEntry dummy;
            storeQueue.push_back(dummy);
            SQEntries++;
        }
    } else {
        SQEntries = size_plus_sentinel;
    }

    assert(SQEntries <= 256);
}

template <class Impl>
void
LSQUnit<Impl>::insert(DynInstPtr &inst)
{
    assert(inst->isMemRef());

    assert(inst->isLoad() || inst->isStore());

    if (inst->isLoad()) {
        insertLoad(inst);
    } else {
        insertStore(inst);
    }

    inst->setInLSQ();
}

template <class Impl>
void
LSQUnit<Impl>::insertLoad(DynInstPtr &load_inst)
{
    assert((loadTail + 1) % LQEntries != loadHead);
    assert(loads < LQEntries);

    DPRINTF(LSQUnit, "Inserting load PC %s, idx:%i [sn:%lli]\n",
            load_inst->pcState(), loadTail, load_inst->seqNum);
    DPRINTF(LSQUnit, "  readyToExpose=%d\n", load_inst->readyToExpose());

    load_inst->lqIdx = loadTail;

    if (stores == 0) {  // stores: the number of store instruction is SQ
        load_inst->sqIdx = -1;
    } else {
        load_inst->sqIdx = storeTail;
    }

    loadQueue[loadTail] = load_inst;

    // Jiyong, MLDOM: prepate an entry in LocPred for the load
    if (cpu->enableMLDOM) {
        DPRINTF(JY, "Prepare for load PC %s [sn:%lli]\n",
                load_inst->pcState(), load_inst->seqNum);
        cpu->locationPredictor->prepare(load_inst->pcState().instAddr(), load_inst->seqNum);
    }

    incrLdIdx(loadTail);

    ++loads;

}

template <class Impl>
void
LSQUnit<Impl>::insertStore(DynInstPtr &store_inst)
{
    // Make sure it is not full before inserting an instruction.
    assert((storeTail + 1) % SQEntries != storeHead);
    assert(stores < SQEntries);

    DPRINTF(LSQUnit, "Inserting store PC %s, idx:%i [sn:%lli]\n",
            store_inst->pcState(), storeTail, store_inst->seqNum);

    store_inst->sqIdx = storeTail;
    store_inst->lqIdx = loadTail;

    storeQueue[storeTail] = SQEntry(store_inst);

    incrStIdx(storeTail);

    ++stores;
}

// It is an empty function? why? [mengjia]
template <class Impl>
typename Impl::DynInstPtr
LSQUnit<Impl>::getMemDepViolator()
{
    DynInstPtr temp = memDepViolator;

    memDepViolator = NULL;

    return temp;
}

// Jiyong, MLDOM
template <class Impl>
typename Impl::DynInstPtr
LSQUnit<Impl>::getOblsMissInst()
{
    DynInstPtr temp = oblsMissInst;

    oblsMissInst = NULL;

    return temp;
}

template <class Impl>
unsigned
LSQUnit<Impl>::numFreeLoadEntries()
{
        //LQ has an extra dummy entry to differentiate
        //empty/full conditions. Subtract 1 from the free entries.
        DPRINTF(LSQUnit, "LQ size: %d, #loads occupied: %d\n", LQEntries, loads);
        return LQEntries - loads - 1;
}

template <class Impl>
unsigned
LSQUnit<Impl>::numFreeStoreEntries()
{
        //SQ has an extra dummy entry to differentiate
        //empty/full conditions. Subtract 1 from the free entries.
        DPRINTF(LSQUnit, "SQ size: %d, #stores occupied: %d\n", SQEntries, stores);
        return SQEntries - stores - 1;

 }

template <class Impl>
void
LSQUnit<Impl>::checkSnoop(PacketPtr pkt)
{
    // Should only ever get invalidations in here
    assert(pkt->isInvalidate());

    int load_idx = loadHead;
    DPRINTF(LSQUnit, "Got snoop for address %#x\n", pkt->getAddr());

    // Only Invalidate packet calls checkSnoop
    assert(pkt->isInvalidate());
    for (int x = 0; x < cpu->numContexts(); x++) {
        ThreadContext *tc = cpu->getContext(x);
        bool no_squash = cpu->thread[x]->noSquashFromTC;
        cpu->thread[x]->noSquashFromTC = true;
        TheISA::handleLockedSnoop(tc, pkt, cacheBlockMask);
        cpu->thread[x]->noSquashFromTC = no_squash;
    }

    Addr invalidate_addr = pkt->getAddr() & cacheBlockMask;

    DynInstPtr ld_inst = loadQueue[load_idx];
    DynInstPtr finished_inst = ld_inst;
    if (ld_inst) {
        Addr load_addr_low = ld_inst->physEffAddrLow & cacheBlockMask;
        Addr load_addr_high = ld_inst->physEffAddrHigh & cacheBlockMask;

        // Check that this snoop didn't just invalidate our lock flag
        // [SafeSpec] also make sure the instruction has been sent out
        // otherwise, we cause unneccessary squash
        if (ld_inst->effAddrValid() && !ld_inst->fenceDelay()
                && (load_addr_low == invalidate_addr
                    || load_addr_high == invalidate_addr)
            && ld_inst->memReqFlags & Request::LLSC)
            TheISA::handleLockedSnoopHit(ld_inst.get());
    }

    // If not match any load entry, then do nothing [mengjia]
    if (load_idx == loadTail)
        return;

    incrLdIdx(load_idx);

    bool force_squash = false;

    while (load_idx != loadTail) {
        DynInstPtr ld_inst = loadQueue[load_idx];

        // [SafeSpce] check snoop violation when the load has
        // been sent out; otherwise, unneccessary squash
        if (!ld_inst->effAddrValid() || ld_inst->strictlyOrdered()
                || ld_inst->fenceDelay()) {
            incrLdIdx(load_idx);
            continue;
        }

        Addr load_addr_low = ld_inst->physEffAddrLow & cacheBlockMask;
        Addr load_addr_high = ld_inst->physEffAddrHigh & cacheBlockMask;

        DPRINTF(LSQUnit, "-- inst [sn:%lli] load_addr: %#x to pktAddr:%#x\n",
                    ld_inst->seqNum, load_addr_low, invalidate_addr);

        if ((load_addr_low == invalidate_addr
             || load_addr_high == invalidate_addr) || force_squash) {
            if (cpu->needsTSO) {
                // If we have a TSO system, as all loads must be ordered with
                // all other loads, this load as well as *all* subsequent loads
                // need to be squashed to prevent possible load reordering.
                force_squash = true;

                // [SafeSpec] in safespec, we do not need to squash
                // the load at the head of LQ,
                // as well as the one do not need validation
                if (cpu->isInvisibleSpec &&
                    (load_idx==loadHead || ld_inst->needExposeOnly())){
                    force_squash = false;
                }
                if (!pkt->isExternalEviction() && cpu->isInvisibleSpec){
                    force_squash = false;
                    ld_inst->clearL1HitHigh();
                    ld_inst->clearL1HitLow();
                }
            }
            if (ld_inst->possibleLoadViolation() || force_squash) {
                DPRINTF(LSQUnit, "Conflicting load at addr %#x [sn:%lli]\n",
                        pkt->getAddr(), ld_inst->seqNum);

                //[SafeSpec] mark the load hit invalidation
                ld_inst->hitInvalidation(true);
                if (pkt->isExternalEviction()){
                    ld_inst->hitExternalEviction(true);
                }
                // Mark the load for re-execution
                ld_inst->fault = std::make_shared<ReExec>();
            } else {
                DPRINTF(LSQUnit, "HitExternal Snoop for addr %#x [sn:%lli]\n",
                        pkt->getAddr(), ld_inst->seqNum);

                // Make sure that we don't lose a snoop hitting a LOCKED
                // address since the LOCK* flags don't get updated until
                // commit.
                if (ld_inst->memReqFlags & Request::LLSC)
                    TheISA::handleLockedSnoopHit(ld_inst.get());

                // If a older load checks this and it's true
                // then we might have missed the snoop
                // in which case we need to invalidate to be sure
                ld_inst->hitExternalSnoop(true);
            }
        }
        incrLdIdx(load_idx);
    }
    return;
}

template <class Impl>
bool
LSQUnit<Impl>::checkPrevLoadsExecuted(int req_idx)
{
    int load_idx = loadHead;
    while (load_idx != req_idx){
        if (!loadQueue[load_idx]->isExecuted()){
            // if at least on load ahead of current load
            // does not finish spec access,
            // then return false
            return false;
        }

        // mengjia, fix bug
        if (cpu->needsTSO && loadQueue[load_idx]->needPostFetch() &&
            !loadQueue[load_idx]->needExposeOnly() &&
            !loadQueue[load_idx]->isExposeCompleted() ) {
            // in TSO we cannot expose if a previous load
            // that needs validation has not validate yet
            return false;
        }

        incrLdIdx(load_idx);
    }

    //if all executed, return true
    return true;
}

template <class Impl>
void
LSQUnit<Impl>::setSpecBufState(RequestPtr expose_req)
{
    Addr req_eff_addr = expose_req->getPaddr() & cacheBlockMask;

    int load_idx = loadHead;
    while (load_idx != loadTail) {
        DynInstPtr ld_inst = loadQueue[load_idx];
        if (ld_inst->effAddrValid()) {
            Addr ld_eff_addr1 = ld_inst->physEffAddrLow  & cacheBlockMask;
            Addr ld_eff_addr2 = ld_inst->physEffAddrHigh & cacheBlockMask;
            if (ld_eff_addr1 == req_eff_addr){
                DPRINTF(JY, "ld [sn:%lli] set SB obsolete low\n", ld_inst->seqNum);
                ld_inst->setSpecBufObsoleteLow();
            } else if (ld_eff_addr2 == req_eff_addr){
                ld_inst->setSpecBufObsoleteHigh();
                DPRINTF(JY, "ld [sn:%lli] set SB obsolete high\n", ld_inst->seqNum);
            }
        }
        incrLdIdx(load_idx);
    }
}


template <class Impl>
int
LSQUnit<Impl>::checkSpecBufHit(RequestPtr req, int req_idx)
{
    Addr req_eff_addr = req->getPaddr() & cacheBlockMask;
    DPRINTF(JY, "checkSpecBufHit for idx=%d, line_addr=0x%lx\n", req_idx, req_eff_addr);
    assert (!loadQueue[req_idx]->isExecuted());

    int load_idx = loadHead;

    /** InvisiSpec version **/
    //while (load_idx != loadTail) {
        //DynInstPtr ld_inst = loadQueue[load_idx];
        //if (ld_inst->effAddrValid()) {
            //Addr ld_eff_addr1 = ld_inst->physEffAddrLow & cacheBlockMask;
            //Addr ld_eff_addr2 = ld_inst->physEffAddrHigh & cacheBlockMask;

            //if ((req_eff_addr == ld_eff_addr1 || req_eff_addr == ld_eff_addr2)
                    //&& ld_inst->oblS_Hit_L0()) {
                ////already in L1, do not copy from buffer
                //return -1;
            //} else {
                //if (ld_inst->isExecuted() && ld_inst->needPostFetch()
                    //&& !ld_inst->isSquashed() && ld_inst->fault == NoFault) {
                    //if (req_eff_addr == ld_eff_addr1 && !ld_inst->isL1HitLow()
                            //&& !ld_inst->isSpecBufObsoleteLow()){
                        //DPRINTF(LSQUnit, "Detected Spec Hit with inst [sn:%lli] "
                            //"and [sn:%lli] (low) at address %#x\n",
                            //loadQueue[req_idx]->seqNum, ld_inst->seqNum,
                            //req_eff_addr);
                        //return load_idx;
                    //} else if ( ld_eff_addr2 !=0  &&
                        //req_eff_addr == ld_eff_addr2 && !ld_inst->isL1HitHigh()
                        //&& !ld_inst->isSpecBufObsoleteHigh()){
                        //DPRINTF(LSQUnit, "Detected Spec Hit with inst [sn:%lli] "
                            //"and [sn:%lli] (high) at address %#x\n",
                            //loadQueue[req_idx]->seqNum, ld_inst->seqNum,
                            //req_eff_addr);
                        //return load_idx;
                    //}
                //}
            //}
        //}
        //incrLdIdx(load_idx);
    //}

    /** Jiyong, SDO: SDO version **/
    while (load_idx != loadTail) {
        DynInstPtr comp_ld_inst = loadQueue[load_idx];
        if (comp_ld_inst->effAddrValid()) {
            Addr ld_eff_addr1 = comp_ld_inst->physEffAddrLow & cacheBlockMask;
            Addr ld_eff_addr2 = comp_ld_inst->physEffAddrHigh & cacheBlockMask;

            if (/*comp_ld_inst->isExecuted() &&*/ comp_ld_inst->needPostFetch() // must be finished obls
                && /*!comp_ld_inst->isSquashed() &&*/ comp_ld_inst->fault == NoFault) { // cannot be squashed & with fault (e.g. memory ordering)
                DPRINTF(JY, "ld[sn=%lli] is a completed specld\n", comp_ld_inst->seqNum);
                if (req_eff_addr == ld_eff_addr1 && !comp_ld_inst->isSpecBufObsoleteLow()) {
                    DPRINTF(JY, "SpecBuf Hit with inst [sn:%lli] and [sn:%lli] (low) at address %#x\n",
                            loadQueue[req_idx]->seqNum, comp_ld_inst->seqNum, req_eff_addr);
                    return load_idx;
                }
                else if (req_eff_addr == ld_eff_addr2 && !comp_ld_inst->isSpecBufObsoleteHigh()) {
                    DPRINTF(JY, "SpecBuf Hit with inst [sn:%lli] and [sn:%lli] (high) at address %#x\n",
                            loadQueue[req_idx]->seqNum, comp_ld_inst->seqNum, req_eff_addr);
                    return load_idx;
                }
            }
        }
        incrLdIdx(load_idx);
    }
    return -1;
}



template <class Impl>
Fault
LSQUnit<Impl>::checkViolations(int load_idx, DynInstPtr &inst)
{
    Addr inst_eff_addr1 = inst->effAddr >> depCheckShift;
    Addr inst_eff_addr2 = (inst->effAddr + inst->effSize - 1) >> depCheckShift;

    /** @todo in theory you only need to check an instruction that has executed
     * however, there isn't a good way in the pipeline at the moment to check
     * all instructions that will execute before the store writes back. Thus,
     * like the implementation that came before it, we're overly conservative.
     */
    while (load_idx != loadTail) {
        DynInstPtr ld_inst = loadQueue[load_idx];
        // [SafeSpec] no need to check violation for unsent load
        // otherwise, unneccessary squash
        if (!ld_inst->effAddrValid() || ld_inst->strictlyOrdered()
                || ld_inst->fenceDelay()) {
            incrLdIdx(load_idx);
            continue;
        }

        Addr ld_eff_addr1 = ld_inst->effAddr >> depCheckShift;
        Addr ld_eff_addr2 =
            (ld_inst->effAddr + ld_inst->effSize - 1) >> depCheckShift;

        if (inst_eff_addr2 >= ld_eff_addr1 && inst_eff_addr1 <= ld_eff_addr2) {
            if (inst->isLoad()) {
                // If this load is to the same block as an external snoop
                // invalidate that we've observed then the load needs to be
                // squashed as it could have newer data
                if (ld_inst->hitExternalSnoop()) {
                    if (!memDepViolator ||
                            ld_inst->seqNum < memDepViolator->seqNum) {
                        DPRINTF(LSQUnit, "Detected fault with inst [sn:%lli] "
                                "and [sn:%lli] at address %#x\n",
                                inst->seqNum, ld_inst->seqNum, ld_eff_addr1);
                        memDepViolator = ld_inst;

                        ++lsqMemOrderViolation;

                        return std::make_shared<GenericISA::M5PanicFault>(
                            "Detected fault with inst [sn:%lli] and "
                            "[sn:%lli] at address %#x\n",
                            inst->seqNum, ld_inst->seqNum, ld_eff_addr1);
                    }
                }

                // Otherwise, mark the load has a possible load violation
                // and if we see a snoop before it's commited, we need to squash
                ld_inst->possibleLoadViolation(true);
                DPRINTF(LSQUnit, "Found possible load violation at addr: %#x"
                        " between instructions [sn:%lli] and [sn:%lli]\n",
                        inst_eff_addr1, inst->seqNum, ld_inst->seqNum);
            } else {
                // A load/store incorrectly passed this store.
                // Check if we already have a violator, or if it's newer
                // squash and refetch.
                if (memDepViolator && ld_inst->seqNum > memDepViolator->seqNum)
                    break;

                DPRINTF(LSQUnit, "Detected fault with inst [sn:%lli] and "
                        "[sn:%lli] at address %#x\n",
                        inst->seqNum, ld_inst->seqNum, ld_eff_addr1);
                memDepViolator = ld_inst;

                ++lsqMemOrderViolation;

                return std::make_shared<GenericISA::M5PanicFault>(
                    "Detected fault with "
                    "inst [sn:%lli] and [sn:%lli] at address %#x\n",
                    inst->seqNum, ld_inst->seqNum, ld_eff_addr1);
            }
        }

        incrLdIdx(load_idx);
    }
    return NoFault;
}




template <class Impl>
Fault
LSQUnit<Impl>::executeLoad(DynInstPtr &inst)
{
    using namespace TheISA;
    // Execute a specific load.
    Fault load_fault = NoFault;

    DPRINTF(LSQUnit, "Executing load PC %s, [sn:%lli]\n",
            inst->pcState(), inst->seqNum);

    assert(!inst->isSquashed());

    // use ISA interface to generate correct access request
    // initiateAcc is implemented in dyn_inst_impl.hh
    // The interface calls corresponding ISA defined function
    // check buld/ARM/arch/generic/memhelper.hh for more info [mengjia]
    load_fault = inst->initiateAcc();

    // if translation delay, deferMem [mengjia]
    // in the case it is not the correct time to send the load
    // also defer it
    if ( (inst->isTranslationDelayed() || inst->fenceDelay()
                || inst->specTLBTransDelayed()) &&
        load_fault == NoFault)
        return load_fault;

    // If the instruction faulted or predicated false, then we need to send it
    // along to commit without the instruction completing.
    //
    // if it is faulty, not execute it, send it to commit, and commit statge will deal with it
    // here is signling the ROB, the inst can commit [mengjia]
    if (load_fault != NoFault || !inst->readPredicate()) {
        // Send this instruction to commit, also make sure iew stage
        // realizes there is activity.  Mark it as executed unless it
        // is a strictly ordered load that needs to hit the head of
        // commit.
        if (!inst->readPredicate())
            inst->forwardOldRegs();
        DPRINTF(LSQUnit, "Load [sn:%lli] not executed from %s\n",
                inst->seqNum,
                (load_fault != NoFault ? "fault" : "predication"));
        if (!(inst->hasRequest() && inst->strictlyOrdered()) ||
            inst->isAtCommit()) {
            inst->setExecuted();
        }
        iewStage->instToCommit(inst);
        iewStage->activityThisCycle();
    } else {
        assert(inst->effAddrValid());
        int load_idx = inst->lqIdx;
        incrLdIdx(load_idx);

        if (checkLoads)
            return checkViolations(load_idx, inst);
    }

    return load_fault;
}

template <class Impl>
Fault
LSQUnit<Impl>::executeStore(DynInstPtr &store_inst)
{
    using namespace TheISA;
    // Make sure that a store exists.
    assert(stores != 0);

    int store_idx = store_inst->sqIdx;

    DPRINTF(LSQUnit, "Executing store PC %s [sn:%lli]\n",
            store_inst->pcState(), store_inst->seqNum);

    assert(!store_inst->isSquashed());

    // Check the recently completed loads to see if any match this store's
    // address.  If so, then we have a memory ordering violation.
    int load_idx = store_inst->lqIdx;

    // TODO: Check whether this store tries to get an exclusive copy 
    // of target line [mengjia]
    Fault store_fault = store_inst->initiateAcc();

    if (store_inst->isTranslationDelayed() &&
        store_fault == NoFault)
        return store_fault;

    if (!store_inst->readPredicate()) {
        DPRINTF(LSQUnit, "Store [sn:%lli] not executed from predication\n",
                store_inst->seqNum);
        store_inst->forwardOldRegs();
        return store_fault;
    }

    if (storeQueue[store_idx].size == 0) {
        DPRINTF(LSQUnit,"Fault on Store PC %s, [sn:%lli], Size = 0\n",
                store_inst->pcState(), store_inst->seqNum);

        return store_fault;
    }

    assert(store_fault == NoFault);

    if (store_inst->isStoreConditional()) {
        // Store conditionals need to set themselves as able to
        // writeback if we haven't had a fault by here.
        storeQueue[store_idx].canWB = true;

        ++storesToWB;
    }

    return checkViolations(load_idx, store_inst);

}

template <class Impl>
void
LSQUnit<Impl>::commitLoad()
{
    assert(loadQueue[loadHead]);

    DPRINTF(LSQUnit, "Committing head load instruction, PC %s\n",
            loadQueue[loadHead]->pcState());

    loadQueue[loadHead] = NULL;

    incrLdIdx(loadHead);

    --loads;
}

template <class Impl>
void
LSQUnit<Impl>::commitLoads(InstSeqNum &youngest_inst)
{
    assert(loads == 0 || loadQueue[loadHead]);

    while (loads != 0 && loadQueue[loadHead]->seqNum <= youngest_inst) {
        commitLoad();
    }
}

template <class Impl>
void
LSQUnit<Impl>::commitStores(InstSeqNum &youngest_inst)
{
    assert(stores == 0 || storeQueue[storeHead].inst);

    int store_idx = storeHead;

    while (store_idx != storeTail) {
        assert(storeQueue[store_idx].inst);
        // Mark any stores that are now committed and have not yet
        // been marked as able to write back.
        if (!storeQueue[store_idx].canWB) {
            if (storeQueue[store_idx].inst->seqNum > youngest_inst) {
                break;
            }
            DPRINTF(LSQUnit, "Marking store as able to write back, PC "
                    "%s [sn:%lli]\n",
                    storeQueue[store_idx].inst->pcState(),
                    storeQueue[store_idx].inst->seqNum);

            storeQueue[store_idx].canWB = true;

            ++storesToWB;
        }

        incrStIdx(store_idx);
    }
}

template <class Impl>
void
LSQUnit<Impl>::writebackPendingStore()
{
    if (hasPendingPkt) {
        assert(pendingPkt != NULL);

        if(pendingPkt->isWrite()){
            // If the cache is blocked, this will store the packet for retry.
            if (sendStore(pendingPkt)) {
                storePostSend(pendingPkt);
            }
            pendingPkt = NULL;
            hasPendingPkt = false;
        }
    }
}




// [SafeSpec] update FenceDelay State
/*** [Jiyong,STT] update logic for STT ***/
template <class Impl>
void
LSQUnit<Impl>::updateVisibleState()
{
    int load_idx = loadHead;

    //iterate all the loads and update its fencedelay state accordingly
    while (load_idx != loadTail && loadQueue[load_idx]){

        DynInstPtr inst = loadQueue[load_idx];

        // make sure this instr has VP properly set up
        if (inst->robUpdateVisibleState) {
            // lsq update visible state happens after rob
            inst->lsqUpdateVisibleState = true;
        }

        if (!cpu->loadInExec) {
            // fence (fenceDelay flag is effective)
            if (cpu->STT) {
                // STT + Fence
                if (inst->isArgsTainted() && !inst->fenceDelay())
                    DPRINTF(JY, "WARNING: inst [sn:%lli] has argstainted =True, fenceDelay = False\n", inst->seqNum);
                inst->fenceDelay(!inst->taint_prop_done || inst->isArgsTainted());

                // readyToExpose has no effect since the load will be delayExeucte if it's unsafe
                inst->readyToExpose(true);
            }
            else {
                // NoSTT + Fence
                // !STT, if delay fence when fence is squashable
                if ( (cpu->isFuturistic && inst->isPrevInstsCompleted()) || // (cpu->isFuturistic && inst->isPrevInstsCommitted()) ||
                        (!cpu->isFuturistic && inst->isPrevBrsResolved())){ // (!cpu->isFuturistic && inst->isPrevBrsCommitted())){
                    // here prior instructions are committed so inst is unsquashable
                    if (inst->fenceDelay()){
                        DPRINTF(LSQUnit, "Clear virtual fence for "
                                "inst [sn:%lli] PC %s\n", inst->seqNum, inst->pcState());
                    }
                    inst->fenceDelay(false);
                } else {
                    // here prior instructions are not committed so inst is squashable
                    if (!inst->fenceDelay()){
                        DPRINTF(LSQUnit, "Deffering an inst [sn:%lli] PC %s"
                                " due to virtual fence\n",inst->seqNum, inst->pcState());
                    }
                    inst->fenceDelay(true);
                }

                // readyToExpose has no effect since the load will be delayExeucte if it's unsafe
                inst->readyToExpose(true);
            }
        } else if (cpu->loadInExec && cpu->isInvisibleSpec){
            // invisiSpec (readyToExpose flag is effective)
            if (cpu->STT) {
                // STT + Invisispec
                if (inst->needPostFetch() && !inst->isArgsTainted() && !inst->readyToExpose())
                    ;
                    //++loadsToVLD;
                else if (inst->isArgsTainted() && inst->readyToExpose()) {
                    DPRINTF(LSQUnit, "The load can not be validated "
                            "[sn:%lli] PC %s\n", inst->seqNum, inst->pcState());
                    assert(0);
                }
                inst->readyToExpose(inst->taint_prop_done && !inst->isArgsTainted());

                if (cpu->enableMLDOM) {
                    //if (cpu->delay_on_DRAM_pred && inst->isArgsTainted() && locPred->lookup(inst->pcState().instAddr()) == DRAM) {
                        //DPRINTF(JY, "inst [sn:%lli] has argstainted, and prediction is DRAM ==> delay since delay_on_DRAM is set\n", inst->seqNum);
                        //inst->fenceDelay(true);
                    //}
                    //else if (cpu->delay_on_LLC_pred && inst->isArgsTainted() && locPred->lookup(inst->pcState().instAddr()) == Cache_L3) {
                        //DPRINTF(JY, "inst [sn:%lli] has argstainted, and prediction is LLC ==> delay since delay_on_LLC is set\n", inst->seqNum);
                        //inst->fenceDelay(true);
                    //}
                    //else
                        inst->fenceDelay(false);
                }
                else {
                    inst->fenceDelay(false);
                }
            } else {
                // NoSTT + Invisispec
                if ( (cpu->isFuturistic && inst->isPrevInstsCompleted()) ||
                        (!cpu->isFuturistic && inst->isPrevBrsResolved())) {
                    if (!inst->readyToExpose()){
                        DPRINTF(LSQUnit, "Set readyToExpose for "
                                "inst [sn:%lli] PC %s\n", inst->seqNum, inst->pcState());
                        if (inst->needPostFetch()){
                            ;
                            //++loadsToVLD;
                        }
                    }
                    inst->readyToExpose(true);
                } else {
                    if (inst->readyToExpose()){
                        DPRINTF(LSQUnit, "The load can not be validated "
                                "[sn:%lli] PC %s\n", inst->seqNum, inst->pcState());
                        assert(0);
                        //--loadsToVLD;
                    }
                    inst->readyToExpose(false);
                }

                // we set fenceDelay to false since we don't delay unsafe load in Invisispec ( we issue it safely )
                inst->fenceDelay(false);
            }
        } else {
            // unsafe
            inst->readyToExpose(true);
            inst->isUnsquashable(true);
            inst->fenceDelay(false);
        }

        incrLdIdx(load_idx);
    }
}

// [SafeSpec] validate loads
template <class Impl>
int
LSQUnit<Impl>::exposeLoads()
{
    if(!cpu->isInvisibleSpec){
        assert(loadsToVLD==0 && "ERROR: request validation on Non invisible Spec mode");
    }

    // [SafeSpec] Note:
    // need to iterate from the head every time
    // since the load can be exposed out-of-order
    int loadVLDIdx = loadHead;

    while (loadVLDIdx != loadTail && loadQueue[loadVLDIdx]) {

        // Jiyong: if squash from this instr, break
        if (oblsMissInst) {
            DPRINTF(JY, "current Obls miss inst: sn:%lli\n", oblsMissInst->seqNum);
            if (loadQueue[loadVLDIdx]->seqNum == oblsMissInst->seqNum)
                break;
        }

        // Jiyong: if the load is squashed, skip it
        if (loadQueue[loadVLDIdx]->isSquashed()){
            incrLdIdx(loadVLDIdx);
            continue;
        }

        // Jiyong, MLDOM: if the load is load-store forwarding && pass VP, update locPred here
        if (cpu->enableMLDOM) {
            if (loadQueue[loadVLDIdx]->isExecuted() &&
                loadQueue[loadVLDIdx]->if_ldStFwd    &&
                loadQueue[loadVLDIdx]->readyToExpose())
                updateLocationPredictor(loadQueue[loadVLDIdx], 3);
        }


        // skip the loads that either do not need to expose
        // or exposed already
        if(!loadQueue[loadVLDIdx]->needPostFetch()
                || loadQueue[loadVLDIdx]->isExposeSent() ){
            incrLdIdx(loadVLDIdx);
            continue;
        }

        DynInstPtr load_inst = loadQueue[loadVLDIdx];
        if (loadQueue[loadVLDIdx]->fault != NoFault){
            // load is executed, so it wait for expose complete
            // to send it to commit, regardless of whether it is ready
            // to expose
            load_inst->setExposeSent();
            load_inst->setExposeCompleted();
            if (load_inst->isExecuted()){
                DPRINTF(LSQUnit, "Execute finished and gets violation fault."
                    "Send inst [sn:%lli] to commit stage.\n",
                    load_inst->seqNum);
                    iewStage->instToCommit(load_inst);
                    iewStage->activityThisCycle();
            }
            incrLdIdx(loadVLDIdx);
            continue;
        }

        // skip the loads that need expose but are not ready
        if (loadQueue[loadVLDIdx]->needPostFetch()
                && !loadQueue[loadVLDIdx]->readyToExpose()){
            incrLdIdx(loadVLDIdx);
            continue;
        }

        assert(loadQueue[loadVLDIdx]->needPostFetch()
                && loadQueue[loadVLDIdx]->readyToExpose() );

        assert(!load_inst->isCommitted());

        // start preparing expose/validate
        Request *req      = load_inst->postReq;
        Request *sreqLow  = load_inst->postSreqLow;
        Request *sreqHigh = load_inst->postSreqHigh;

        // we should not have both req and sreqLow not NULL
        assert(req);

        DPRINTF(LSQUnit, "Validate/Expose request for inst [sn:%lli]"
            " PC= %s. req=%#x, reqLow=%#x, reqHigh=%#x\n",
            load_inst->seqNum, load_inst->pcState(),
            (Addr)(load_inst->postReq),
            (Addr)(load_inst->postSreqLow), (Addr)(load_inst->postSreqHigh));

        // Jiyong: MLDOM: we create the vld data for both expose and validate. So they are handled like load
        assert (load_inst->effSize == req->getSize());
        if (!load_inst->vldData)
            load_inst->vldData = new uint8_t [load_inst->effSize];

        PacketPtr data_pkt = NULL;
        PacketPtr fst_data_pkt = NULL;
        PacketPtr snd_data_pkt = NULL;

        LSQSenderState *state = new LSQSenderState;
        // Jiyong, MLDOM: in MLDOM, we treat validate as load, as it may do writeback
        if (cpu->enableMLDOM)
            state->isLoad = true;
        else
            state->isLoad = false;

        state->idx = loadVLDIdx;
        state->inst = load_inst;

        // Jiyong, MLDOM: in MLDOM, we treat validate as need validate
        if (cpu->enableMLDOM)
            state->noWB = false;
        else
            state->noWB = true;

        bool split = false;
        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            split = true;
        } else {
            assert(req);
        }

        bool onlyExpose = false;
        if (!split) {
            // Not split packet
            if (!cpu->enableMLDOM) {
                printf("ERROR: vanilla Invisispec is not supported.\n");
                assert (0);
                // Jiyong: Vanilla InvisiSpec
                //if (load_inst->needExposeOnly() || load_inst->isL1HitLow()){
                    //data_pkt = Packet::createExpose(req);
                    //onlyExpose = true;
                //} else {
                    //data_pkt = Packet::createValidate(req);
                    //if (!load_inst->vldData)
                        //load_inst->vldData = new uint8_t[1];
                    //data_pkt->dataStatic(load_inst->vldData);
                //}
            }
            else {
                // Jiyong, MLDOM: SDO protection for TLB
                // if spec TLB was miss before, trigger a squash here
                if (!load_inst->obls_TLB_Hit) {
                    assert (cpu->TLB_defense == 3);
                    // before (tainted) ld sent oblS, it has TLB miss and mis-predict
                    DPRINTF(JY, "load (not split) [sn:%lli] at state %d has TLB miss when tainted, now untainted and attempts to send exp/val\n", 
                            load_inst->seqNum, load_inst->MLDOM_state);
                    load_inst->MLDOM_state = MLDOM_STATE_toBeSquash;  // load will be squashed and issue normal GetS
                    // squash this load due to tlb miss
                    DPRINTF(JY, "A load has Obls TLB miss. The load [sn:%lli] is safe now. Squash it!\n", load_inst->seqNum);
                    if (!oblsMissInst || load_inst->seqNum < oblsMissInst->seqNum) {
                        oblsMissInst = load_inst;
                    }
                    DPRINTF(JY, "Obls TLB Miss load to squash next: sn:%lli\n", oblsMissInst->seqNum);
                    delete state;
                    delete data_pkt;
                    load_inst->doneWB        = false;
                    load_inst->doneWB_byOblS = false;

                    // increment counter
                    numSquashOnSpecTLBMiss++;
                    break;
                }

                // Basic scheme starts
                if (load_inst->MLDOM_state == MLDOM_STATE_AB) {
                    load_inst->MLDOM_state = MLDOM_STATE_ABC;
                    // ld sent OblS; OblS returned ++ ld sent Val
                    // Actions for OblS miss: squash and redo the load
                    assert(load_inst->oblS_Complete);
                    if (!load_inst->oblS_Hit) {
                        DPRINTF(JY, "load (not split) [sn:%lli] current state = AB, next = ABC, was a OblS miss->squash\n", load_inst->seqNum);
                        assert (load_inst->valPred_onMiss);
                        load_inst->MLDOM_state = MLDOM_STATE_toBeSquash;    // load gets squashed and then issues GetS
                        // squash this load due to obls miss
                        DPRINTF(JY, "A load has Obls miss. The load [sn:%lli] is safe now. Squash it!\n", load_inst->seqNum);
                        if (!oblsMissInst || load_inst->seqNum < oblsMissInst->seqNum) {
                            oblsMissInst = load_inst;
                        }
                        DPRINTF(JY, "Obls Miss load to squash next: sn:%lli\n", oblsMissInst->seqNum);
                        delete state;
                        delete data_pkt;
                        load_inst->doneWB        = false;   // the value prediction writeback is removed
                        load_inst->doneWB_byOblS = false;
                        //load_inst->needPostFetch(false);

                        // increment the counter
                        if (load_inst->pred_level == Cache_L1)
                            numSquashOnMiss_SpecLdL0++;
                        else if (load_inst->pred_level == Cache_L2)
                            numSquashOnMiss_SpecLdL1++;
                        else if (load_inst->pred_level == Cache_L3)
                            numSquashOnMiss_SpecLdL2++;
                        else if (load_inst->pred_level == DRAM)
                            numSquashOnMiss_SpecLdMem++;
                        else if (load_inst->pred_level == Perfect_Level)
                            numSquashOnMiss_SpecLdPerfect++;

                        break;
                    }
                    else {
                        DPRINTF(JY, "load (not split) [sn:%lli] current state = AB, next = ABC, was a OblS hit->update locP\n", load_inst->seqNum);
                        // safe and hit, we update the predictor based on the cache level using oblS return
                        updateLocationPredictor(load_inst, 1);
                    }

                    // Actions for OblS hit: for ExposeOnly, we expose; else for L0 hit, we expose; else for LX hit, we valiadate
                    if (cpu->expose_only) {
                        // expose_only is set for single thread. Replace validation with exposure here
                        DPRINTF(JY, "load (not split) [sn:%lli] (continue) current state = AB, next = ABC, send expose because ExposeOnly\n", load_inst->seqNum);
                        // ExposeOnly: Expose
                        load_inst->needSingleExpose = true;
                    }
                    else if (cpu->disable_2ndld) {
                        // disable_2ndld is set. No Exposure/validation here
                        DPRINTF(JY, "load (not split) [sn:%lli] (continue) current state = AB, next = ABC, send no exp/val because disable_2ndld\n", load_inst->seqNum);
                        load_inst->needDeletePostReq(true);
                        load_inst->setExposeSent();
                        load_inst->setExposeCompleted();
                        removedExposes++;
                        return 0;
                    }
                    else if (load_inst->oblS_Hit_L0 || load_inst->needExposeOnly()) {   // InvisiSpec's optimization: hit in L0 needs exposure only, since Invalidation will be propagated to pipeline
                        DPRINTF(JY, "load (not split) [sn:%lli] (continue) current state = AB, next = ABC, send expose\n", load_inst->seqNum);
                        // load returned or hit in L1: Expose
                        load_inst->needSingleExpose = true;
                    }
                    else {
                        DPRINTF(JY, "load (not split) [sn:%lli] current state = AB, next = ABC, send Validate\n", load_inst->seqNum);
                        // otherwise we must validate it
                        load_inst->needSingleValidate = true;
                    }
                }
                else if (load_inst->MLDOM_state == MLDOM_STATE_A) {
                    load_inst->MLDOM_state = MLDOM_STATE_AC;
                    DPRINTF(JY, "load (not split) [sn:%lli] current state = A, next=AC, send Validation\n", load_inst->seqNum);
                    // ld sent OblS ++ ld sent Val
                    // Actions: send Validate
                    load_inst->needSingleValidate = true;
                }
                // Advanced scheme starts
                else if (load_inst->MLDOM_state == MLDOM_STATE_Ab) {
                    load_inst->MLDOM_state = MLDOM_STATE_AbC;
                    DPRINTF(JY, "load (not split) [sn:%lli] current state = Ab, next=AbC, writeback delayed pkt\n", load_inst->seqNum);
                    // ld sent OblS; early hit returns ++ ld send Val
                    // Action: 1) we need to writeback the data now
                    //         2) for L0 hit, we exposure; for LX hit, we validate
                    assert(load_inst->pkt_delayed_wb);
                    if (!load_inst->doneWB) { // packet sending may fail, disallow a second writeback
                        PacketPtr pkt_ptr = load_inst->pkt_delayed_wb;
                        for (int i = 0; i < pkt_ptr->getSize(); i++) {
                            DPRINTF(JY, "writeback delay data [sn:%lli,%lli]: %d:%d\n", load_inst->seqNum, pkt_ptr->seqNum, i, pkt_ptr->getPtr<uint8_t>()[i]);
                        }
                        writeback(load_inst, load_inst->pkt_delayed_wb);
                    }
                    load_inst->doneWB = true;
                    load_inst->doneWB_byOblS = true;
                    assert(load_inst->oblS_Hit);

                    // safe and hit, we update the predictor based on the cache level using OblS return
                    updateLocationPredictor(load_inst, 1);

                    if (cpu->expose_only) {  // for perfectExposeOnly, we only do exposure
                        // expose_only is set for single thread. Replace validation with exposure here
                        DPRINTF(JY, "load (not split) [sn:%lli] current = Ab, next = AbC, send single expose because ExposeOnly\n", load_inst->seqNum);
                        // ExposeOnly: Expose
                        load_inst->needSingleExpose = true;
                    }
                    else if (cpu->disable_2ndld) {
                        // disable_2nd ld is set. No Exposure/validation here
                        DPRINTF(JY, "load (not split) [sn:%lli] current = Ab, next = AbC, send no exp/val because disable_2ndld\n", load_inst->seqNum);
                        load_inst->needDeletePostReq(true);
                        load_inst->setExposeSent();
                        load_inst->setExposeCompleted();
                        removedExposes++;
                        return 0;
                    }
                    else if (load_inst->oblS_Hit_L0 || load_inst->needExposeOnly()) { // InvisiSpec's optimization: hit in L0 needs exposure only, since invalidation will be propagated to pipeline
                        // load returned or hit in L1: Expose
                        DPRINTF(JY, "load (not split) [sn:%lli] current = Ab, next = AbC, send single expose\n", load_inst->seqNum);
                        load_inst->needSingleExpose = true;
                    }
                    else {
                        // otherwise we must validate it
                        DPRINTF(JY, "load (not split) [sn:%lli] current = Ab, next = AbC, send single validate\n", load_inst->seqNum);
                        load_inst->needSingleValidate = true;
                    }
                }
                else if (load_inst->needSingleExpose || load_inst->needSingleValidate) {
                    DPRINTF(JY, "load (not split) [sn:%lli] previous expose/validate is blocked. Now it's in state %d\n", load_inst->seqNum, load_inst->MLDOM_state);
                }
                else if (load_inst->MLDOM_state == MLDOM_STATE_toBeSquash) {
                    // do nothing. Ignore all succeeding loads. This instr will squash later
                    break;
                }
                else {
                    printf("ERROR: unknown MLDOM state transition. current state = %d\n", load_inst->MLDOM_state);
                    assert(0);
                }
            }

            // in perfect-unsafe, obls hit rate is 100%, we don't need to perform validation
            if (cpu->pred_type == Perfect) {
                if (!dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe()) {
                    // perfect && unsafe, only needs exposure
                    load_inst->needSingleExpose = true;
                    load_inst->needSingleValidate = false;
                }
            }

            if (load_inst->needSingleExpose) {
                assert(!load_inst->needSingleValidate);
                DPRINTF(JY, "load [sn:%lli] tries to issue a single expose\n", load_inst->seqNum);

                data_pkt = Packet::createExpose(req);

                onlyExpose = true;
            }
            if (load_inst->needSingleValidate) {
                assert(!load_inst->needSingleExpose);
                DPRINTF(JY, "load [sn:%lli] tries to issue a single validate\n", load_inst->seqNum);

                data_pkt = Packet::createValidate(req);

                onlyExpose = false;
            }

            data_pkt->dataStatic(load_inst->vldData);
            data_pkt->senderState = state;
            data_pkt->reqIdx = loadVLDIdx;
            data_pkt->seqNum = load_inst->seqNum;
            data_pkt->setFirst();
            fst_data_pkt = data_pkt;

            DPRINTF(LSQUnit, "contextid = %d\n", req->contextId());
        } else {
            // Jiyong: Split packet
            if (!cpu->enableMLDOM) {
                printf("ERROR: vanilla invisispec is not supported.\n");
                assert(0);
                // Jiyong: Vanilla InvisiSpec
                // allocate memory if we need at least one validation
                //if (!load_inst->needExposeOnly() &&
                    //(!load_inst->isL1HitLow() || !load_inst->isL1HitHigh())){
                    //if (!load_inst->vldData)
                        //load_inst->vldData = new uint8_t[2];
                //} else {
                    //onlyExpose = true;
                //}

                //// Create the split packets. - first one
                //if (load_inst->needExposeOnly() || load_inst->isL1HitLow()){
                    //data_pkt = Packet::createExpose(sreqLow);
                //}else{
                    //data_pkt = Packet::createValidate(sreqLow);
                    //assert(load_inst->vldData);
                    //data_pkt->dataStatic(load_inst->vldData);
                //}

                //// Create the split packets. - second one
                //if (load_inst->needExposeOnly() || load_inst->isL1HitHigh()){
                    //snd_data_pkt = Packet::createExpose(sreqHigh);
                //} else {
                    //snd_data_pkt = Packet::createValidate(sreqHigh);
                    //assert(load_inst->vldData);
                    //snd_data_pkt->dataStatic(&(load_inst->vldData[1]));
                //}
            }
            else {
                // Jiyong, MLDOM: SDO protection for TLB
                // if spec TLB was miss before, trigger a squash here
                if (!load_inst->obls_TLB_Hit) {
                    assert (cpu->TLB_defense == 3);
                    // before (tainted) ld sent oblS, it has TLB miss and mis-predict
                    DPRINTF(JY, "load (not split) [sn:%lli] at state %d has TLB miss when tainted, now untainted and attempts to send exp/val\n", 
                            load_inst->seqNum, load_inst->MLDOM_state);
                    load_inst->MLDOM_state = MLDOM_STATE_toBeSquash;  // load will be squashed and issue normal GetS
                    // squash this load due to tlb miss
                    DPRINTF(JY, "A load has Obls TLB miss. The load [sn:%lli] is safe now. Squash it!\n", load_inst->seqNum);
                    if (!oblsMissInst || load_inst->seqNum < oblsMissInst->seqNum) {
                        oblsMissInst = load_inst;
                    }
                    DPRINTF(JY, "Obls TLB Miss load to squash next: sn:%lli\n", oblsMissInst->seqNum);
                    delete state;
                    delete data_pkt;
                    load_inst->doneWB        = false;
                    load_inst->doneWB_byOblS = false;

                    // increment counter
                    numSquashOnSpecTLBMiss++;
                    break;
                }

                // Basic scheme starts
                if (load_inst->MLDOM_state == MLDOM_STATE_AB) {
                    load_inst->MLDOM_state = MLDOM_STATE_ABC;
                    // ld sent OblS; OblS returned ++ ld sends Val
                    // Actions for OblS miss: squash and redo the load
                    assert(load_inst->oblS_Complete);
                    if (!load_inst->oblS_Hit) {
                        DPRINTF(JY, "load (split) [sn:%lli] current state = AB, next = ABC, was a OblS miss->squash\n", load_inst->seqNum);
                        assert (load_inst->valPred_onMiss);
                        load_inst->MLDOM_state = MLDOM_STATE_toBeSquash;
                        // squash this load
                        DPRINTF(JY, "A load has Obls miss. The load [sn:%lli] is safe now. Squash it!\n", load_inst->seqNum);
                        if (!oblsMissInst || load_inst->seqNum < oblsMissInst->seqNum) {
                            oblsMissInst = load_inst;
                        }
                        DPRINTF(JY, "Obls Miss load to squash next: sn:%lli\n", oblsMissInst->seqNum);
                        delete state;
                        delete data_pkt;
                        load_inst->doneWB        = false;   // the value prediction writeback is removed
                        load_inst->doneWB_byOblS = false;
                        //load_inst->needPostFetch(false);

                        // increment the counter
                        if (load_inst->pred_level == Cache_L1)
                            numSquashOnMiss_SpecLdL0++;
                        else if (load_inst->pred_level == Cache_L2)
                            numSquashOnMiss_SpecLdL1++;
                        else if (load_inst->pred_level == Cache_L3)
                            numSquashOnMiss_SpecLdL2++;
                        else if (load_inst->pred_level == DRAM)
                            numSquashOnMiss_SpecLdMem++;
                        else if (load_inst->pred_level == Perfect_Level)
                            numSquashOnMiss_SpecLdPerfect++;

                        break;
                    }
                    else {
                        DPRINTF(JY, "load (split) [sn:%lli] current state = AB, next = ABC, was a OblS hit->update locP\n", load_inst->seqNum);
                        // safe and hit, we update the predictor based on the cache level using oblS return
                        updateLocationPredictor(load_inst, 1);
                    }

                    // Actions for OblS hit: for PerfectExposeOnly, we expose; else for L0 hit, we expose; else for LX hit, we validate
                    if (cpu->expose_only) {
                        DPRINTF(JY, "load (split) [sn:%lli] (continue) current state = AB, next = ABC, send expose because PerfectExposeOnly\n", load_inst->seqNum);
                        // perfectExposeOnly: Expose
                        load_inst->needDoubleExpose = true;
                    }
                    else if (cpu->disable_2ndld) {
                        // disable_2nd ld is set. No Exposure/validation here
                        DPRINTF(JY, "load (split) [sn:%lli] current = AB, next = ABC, send no exp/val because disable_2ndld\n", load_inst->seqNum);
                        load_inst->needDeletePostReq(true);
                        load_inst->setExposeSent();
                        load_inst->setExposeCompleted();
                        removedExposes++;
                        return 0;
                    }
                    else if (load_inst->oblS_Hit_L0 || load_inst->needExposeOnly()) {   // InvisiSpec's optimization
                        DPRINTF(JY, "load (split) [sn:%lli] (continue) current state = AB, next = ABC, send expose\n", load_inst->seqNum);
                        // we will send two exposes, no validate
                        load_inst->needDoubleExpose = true;
                    }
                    else {
                        DPRINTF(JY, "load (split) [sn:%lli] current state = AB, next = ABC, send Validate\n", load_inst->seqNum);
                        // otherwise we send two validate
                        load_inst->needDoubleValidate = true;
                    }
                }
                else if (load_inst->MLDOM_state == MLDOM_STATE_A) {
                    load_inst->MLDOM_state = MLDOM_STATE_AC;
                    // ld sent OblS ++ ld sends Val
                    // Actions: send Validate
                    DPRINTF(JY, "load (split) [sn:%lli] current state = A, next=AC, send Validation\n", load_inst->seqNum);

                    // create the split packets
                    load_inst->needDoubleValidate = true;
                }
                // Advanced scheme starts
                else if (load_inst->MLDOM_state == MLDOM_STATE_Ab) {
                    load_inst->MLDOM_state = MLDOM_STATE_AbC;
                    DPRINTF(JY, "load (split) [sn:%lli] current state = Ab, next=AbC, writeback delayed pkt\n", load_inst->seqNum);
                    // ld sent OblS; early hit returns ++ ld send Val
                    // Action: 1) we need to writeback the data now
                    //         2) for L0 hit, we exposure; for LX hit, we validate
                    assert(load_inst->pkt_delayed_wb);
                    if (!load_inst->doneWB) { // packet sending may fail, disallow a second writeback
                        PacketPtr pkt_ptr = load_inst->pkt_delayed_wb;
                        for (int i = 0; i < pkt_ptr->getSize(); i++) {
                            DPRINTF(JY, "writeback delay data [sn:%lli,%lli]: %d:%d\n", load_inst->seqNum, pkt_ptr->seqNum, i, pkt_ptr->getPtr<uint8_t>()[i]);
                        }
                        writeback(load_inst, load_inst->pkt_delayed_wb);
                    }
                    load_inst->doneWB = true;
                    load_inst->doneWB_byOblS = true;
                    assert(load_inst->oblS_Hit);

                    // safe and hit, we update the predictor based on the cache level using OblS return
                    updateLocationPredictor(load_inst, 1);

                    if (cpu->expose_only) { // for perfectExposeOnly, we only do exposure
                        DPRINTF(JY, "load (split) [sn:%lli] current = Ab, next = AbC, send double expose because ExposeOnly\n", load_inst->seqNum);
                        load_inst->needDoubleExpose = true;
                    }
                    else if (cpu->disable_2ndld) {
                        // disable_2nd ld is set. No Exposure/validation here
                        DPRINTF(JY, "load (split) [sn:%lli] current = Ab, next = AbC, send no exp/val because disable_2ndld\n", load_inst->seqNum);
                        load_inst->needDeletePostReq(true);
                        load_inst->setExposeSent();
                        load_inst->setExposeCompleted();
                        removedExposes++;
                        return 0;
                    }
                    else if (load_inst->oblS_Hit_L0 || load_inst->needExposeOnly()) {   // InvisiSpec's optimization
                        // we will send two exposes, no validate
                        DPRINTF(JY, "load (split) [sn:%lli] current = Ab, next = AbC, send double expose\n", load_inst->seqNum);
                        load_inst->needDoubleExpose = true;
                    }
                    else {
                        // otherwise we validate it
                        DPRINTF(JY, "load (split) [sn:%lli] current = Ab, next = AbC, send double validate\n", load_inst->seqNum);
                        load_inst->needDoubleValidate = true;
                    }
                }
                else if (load_inst->needDoubleExpose || load_inst->needDoubleValidate) {
                    DPRINTF(JY, "load (split) [sn:%lli] previous expose/validate is blocked. Now it's in state %d\n", load_inst->seqNum, load_inst->MLDOM_state);
                }
                else if (load_inst->MLDOM_state == MLDOM_STATE_toBeSquash) {
                    // do nothing. This instr will squash later
                    break;
                }
                else {
                    printf("ERROR: unknown MLDOM state transition. Current state = %d\n", load_inst->MLDOM_state);
                    assert(0);
                }
            }

            // in perfect-unsafe, obls hit rate is 100%, we don't need to perform validation
            if (cpu->pred_type == Perfect) {
                if (!dynamic_cast<LocPred_perfect*>(cpu->locationPredictor)->is_perfect_safe()) {
                    // perfect && unsafe, only needs exposure
                    load_inst->needDoubleExpose = true;
                    load_inst->needDoubleValidate = false;
                }
            }

            if (load_inst->needDoubleExpose) {
                assert (!load_inst->needDoubleValidate);
                DPRINTF(JY, "load [sn:%lli] tries to issue a double expose\n", load_inst->seqNum);

                data_pkt     = Packet::createExpose(req);
                fst_data_pkt = Packet::createExpose(sreqLow);
                snd_data_pkt = Packet::createExpose(sreqHigh);

                onlyExpose = true;
            }
            if (load_inst->needDoubleValidate) {
                assert (!load_inst->needDoubleExpose);
                DPRINTF(JY, "load [sn:%lli] tries to issue a double validate\n", load_inst->seqNum);

                data_pkt     = Packet::createValidate(req);
                fst_data_pkt = Packet::createValidate(sreqLow);
                snd_data_pkt = Packet::createValidate(sreqHigh);

                onlyExpose = false;
            }

            data_pkt->dataStatic(load_inst->vldData);
            data_pkt->senderState = state;
            data_pkt->seqNum = load_inst->seqNum;

            fst_data_pkt->dataStatic(load_inst->vldData);
            fst_data_pkt->setFirst();
            fst_data_pkt->senderState = state;
            fst_data_pkt->reqIdx = loadVLDIdx;
            fst_data_pkt->seqNum = load_inst->seqNum;
            fst_data_pkt->isSplit = true;

            snd_data_pkt->dataStatic(load_inst->vldData + sreqLow->getSize());
            snd_data_pkt->senderState = state;
            snd_data_pkt->reqIdx = loadVLDIdx;
            snd_data_pkt->seqNum = load_inst->seqNum;
            snd_data_pkt->isSplit = true;

            state->isSplit = true;
            state->outstanding = 2;
            state->mainPkt = data_pkt;

            DPRINTF(LSQUnit, "contextid = %d, %d\n",
                    sreqLow->contextId(), sreqHigh->contextId());
        }

        assert(!req->isStrictlyOrdered());
        assert(!req->isMmappedIpr());

        DPRINTF(LSQUnit, "D-Cache: Validating/Exposing load idx:%i PC:%s "
                "to Addr:%#x, data:%#x [sn:%lli]\n",
                loadVLDIdx, load_inst->pcState(),
                //FIXME: resultData not memData
                req->getPaddr(), (int)*(load_inst->memData),
                load_inst->seqNum);

        bool successful_expose = true;
        bool completedFirst = false;

        if (!dcachePort->sendTimingReq(fst_data_pkt)){
            DPRINTF(IEW, "D-Cache became blocked when "
                "validating [sn:%lli], will retry later\n",
                load_inst->seqNum);
            successful_expose = false;
        } else {
            if (split) {
                // If split, try to send the second packet too
                completedFirst = true;
                assert(snd_data_pkt);

                if (!dcachePort->sendTimingReq(snd_data_pkt)){
                    state->complete();
                    state->cacheBlocked = true;
                    successful_expose = false;
                    DPRINTF(IEW, "D-Cache became blocked when validating"
                        " [sn:%lli] second packet, will retry later\n",
                        load_inst->seqNum);
                }
            }
        }

        if (!successful_expose){
            DPRINTF(JY, "Expose/Validate for load [sn:%lli] is blocked\n", load_inst->seqNum);
            load_inst->ExposeValidateBlocked = true;    // Jiyong, MLDOM:
            if (!split) {
                delete state;
                delete data_pkt;
            }else{
                if (!completedFirst){
                    delete state;
                    delete data_pkt;
                    delete fst_data_pkt;
                    delete snd_data_pkt;
                } else {
                    delete data_pkt;
                    delete snd_data_pkt;
                    // the packet and the state will be deleted on return
                }
            }
            //cpu->wakeCPU();

            // commnet for OoO validation
            //++lsqCacheBlocked;
            //++lsqCacheBlockedByExpVal;
            //break;
            incrLdIdx(loadVLDIdx);
        } else {
            // Jiyong, MLDOM:
            load_inst->ExposeValidateBlocked = false;
            load_inst->needSingleExpose   = false;
            load_inst->needSingleValidate = false;
            load_inst->needDoubleExpose   = false;
            load_inst->needDoubleValidate = false;
            DPRINTF(JY, "A load [sn:%lli] just successfully issued a Exp/Val\n", load_inst->seqNum);
            // Here is to fix memory leakage
            // it is ugly, but we have to do it now.
            load_inst->needDeletePostReq(false);

            // if all the packets we sent out is expose,
            // we assume the expose is alreay completed
            if (onlyExpose) {
                DPRINTF(JY, "load [sn:%lli] is exposure. Mark it as ExposeCompleted()\n", load_inst->seqNum);
                load_inst->setExposeCompleted();
                numExposes++;
            } else {
                numValidates++;
            }
            if (load_inst->needExposeOnly()){
                numConvertedExposes++;
            }
            if (load_inst->isExecuted() && load_inst->isExposeCompleted()
                    && !load_inst->isSquashed()){
                DPRINTF(LSQUnit, "Expose finished. Execution done."
                    "Send inst [sn:%lli] to commit stage.\n",
                    load_inst->seqNum);
                    //iewStage->instToCommit(load_inst);
                    //iewStage->activityThisCycle();
            } else{
                DPRINTF(LSQUnit, "Need validation or execution not finishes."
                    "Need to wait for readResp/validateResp "
                    "for inst [sn:%lli].\n",
                    load_inst->seqNum);
            }

            load_inst->setExposeSent();
            incrLdIdx(loadVLDIdx);

            if (!onlyExpose) {
                if (!split) {
                    setSpecBufState(req);
                } else {
                    setSpecBufState(sreqLow);
                    setSpecBufState(sreqHigh);
                }
            }
        }
    } // while (loadVLDIdx != loadTail && loadQueue[loadVLDIdx]) {

    //DPRINTF(LSQUnit, "Send validate/expose for %d insts. loadsToVLD=%d"
            //". loadHead=%d. loadTail=%d.\n",
            //old_loadsToVLD-loadsToVLD, loadsToVLD, loadHead,
            //loadTail);

    //printf("expose loads end: loadsToVLD=%d\n", loadsToVLD);
    assert(loads>=0);
    //assert(loadsToVLD >= 0);

    //return old_loadsToVLD-loadsToVLD;
    return 0;
}




template <class Impl>
void
LSQUnit<Impl>::writebackStores()
{
    // First writeback the second packet from any split store that didn't
    // complete last cycle because there weren't enough cache ports available.
    if (TheISA::HasUnalignedMemAcc) {
        writebackPendingStore();
    }

    while (storesToWB > 0 &&
           storeWBIdx != storeTail &&
           storeQueue[storeWBIdx].inst &&
           storeQueue[storeWBIdx].canWB &&
           ((!cpu->needsTSO) || (!storeInFlight)) &&
           usedStorePorts < cacheStorePorts) {

        if (isStoreBlocked) {
            DPRINTF(LSQUnit, "Unable to write back any more stores, cache"
                    " is blocked on stores!\n");
            break;
        }

        // Store didn't write any data so no need to write it back to
        // memory.
        if (storeQueue[storeWBIdx].size == 0) {
            completeStore(storeWBIdx);

            incrStIdx(storeWBIdx);

            continue;
        }

        ++usedStorePorts;

        if (storeQueue[storeWBIdx].inst->isDataPrefetch()) {
            incrStIdx(storeWBIdx);

            continue;
        }

        assert(storeQueue[storeWBIdx].req);
        assert(!storeQueue[storeWBIdx].committed);

        if (TheISA::HasUnalignedMemAcc && storeQueue[storeWBIdx].isSplit) {
            assert(storeQueue[storeWBIdx].sreqLow);
            assert(storeQueue[storeWBIdx].sreqHigh);
        }

        DynInstPtr inst = storeQueue[storeWBIdx].inst;

        Request *req = storeQueue[storeWBIdx].req;
        RequestPtr sreqLow = storeQueue[storeWBIdx].sreqLow;
        RequestPtr sreqHigh = storeQueue[storeWBIdx].sreqHigh;

        storeQueue[storeWBIdx].committed = true;

        assert(!inst->memData);
        inst->memData = new uint8_t[req->getSize()];

        if (storeQueue[storeWBIdx].isAllZeros)
            memset(inst->memData, 0, req->getSize());
        else
            memcpy(inst->memData, storeQueue[storeWBIdx].data, req->getSize());

        PacketPtr data_pkt;
        PacketPtr snd_data_pkt = NULL;

        LSQSenderState *state = new LSQSenderState;
        state->isLoad = false;
        state->idx = storeWBIdx;
        state->inst = inst;

        if (!TheISA::HasUnalignedMemAcc || !storeQueue[storeWBIdx].isSplit) {

            // Build a single data packet if the store isn't split.
            data_pkt = Packet::createWrite(req);
            data_pkt->dataStatic(inst->memData);
            data_pkt->senderState = state;
        } else {
            // Create two packets if the store is split in two.
            data_pkt = Packet::createWrite(sreqLow);
            snd_data_pkt = Packet::createWrite(sreqHigh);

            data_pkt->dataStatic(inst->memData);
            snd_data_pkt->dataStatic(inst->memData + sreqLow->getSize());

            data_pkt->senderState = state;
            snd_data_pkt->senderState = state;

            state->isSplit = true;
            state->outstanding = 2;

            // Can delete the main request now.
            delete req;
            req = sreqLow;
        }

        DPRINTF(LSQUnit, "D-Cache: Writing back store idx:%i PC:%s "
                "to Addr:%#x, data:%#x [sn:%lli]\n",
                storeWBIdx, inst->pcState(),
                req->getPaddr(), (int)*(inst->memData),
                inst->seqNum);

        // @todo: Remove this SC hack once the memory system handles it.
        if (inst->isStoreConditional()) {
            assert(!storeQueue[storeWBIdx].isSplit);
            // Disable recording the result temporarily.  Writing to
            // misc regs normally updates the result, but this is not
            // the desired behavior when handling store conditionals.
            inst->recordResult(false);
            bool success = TheISA::handleLockedWrite(inst.get(), req, cacheBlockMask);
            inst->recordResult(true);

            if (!success) {
                // Instantly complete this store.
                DPRINTF(LSQUnit, "Store conditional [sn:%lli] failed.  "
                        "Instantly completing it.\n",
                        inst->seqNum);
                WritebackEvent *wb = new WritebackEvent(inst, data_pkt, this);
                cpu->schedule(wb, curTick() + 1);
                completeStore(storeWBIdx);
                incrStIdx(storeWBIdx);
                continue;
            }
        } else {
            // Non-store conditionals do not need a writeback.
            state->noWB = true;
        }

        bool split =
            TheISA::HasUnalignedMemAcc && storeQueue[storeWBIdx].isSplit;

        ThreadContext *thread = cpu->tcBase(lsqID);

        if (req->isMmappedIpr()) {
            assert(!inst->isStoreConditional());
            TheISA::handleIprWrite(thread, data_pkt);
            delete data_pkt;
            if (split) {
                assert(snd_data_pkt->req->isMmappedIpr());
                TheISA::handleIprWrite(thread, snd_data_pkt);
                delete snd_data_pkt;
                delete sreqLow;
                delete sreqHigh;
            }
            delete state;
            delete req;
            completeStore(storeWBIdx);
            incrStIdx(storeWBIdx);
        } else if (!sendStore(data_pkt)) {
            DPRINTF(IEW, "D-Cache became blocked when writing [sn:%lli], will"
                    "retry later\n",
                    inst->seqNum);

            // Need to store the second packet, if split.
            if (split) {
                state->pktToSend = true;
                state->pendingPacket = snd_data_pkt;
            }
        } else {

            // If split, try to send the second packet too
            if (split) {
                assert(snd_data_pkt);

                // Ensure there are enough ports to use.
                if (usedStorePorts < cacheStorePorts) {
                    ++usedStorePorts;
                    if (sendStore(snd_data_pkt)) {
                        storePostSend(snd_data_pkt);
                    } else {
                        DPRINTF(IEW, "D-Cache became blocked when writing"
                                " [sn:%lli] second packet, will retry later\n",
                                inst->seqNum);
                    }
                } else {

                    // Store the packet for when there's free ports.
                    assert(pendingPkt == NULL);
                    pendingPkt = snd_data_pkt;
                    hasPendingPkt = true;
                }
            } else {

                // Not a split store.
                storePostSend(data_pkt);
            }
        }
    }

    // Not sure this should set it to 0.
    usedStorePorts = 0;

    assert(stores >= 0 && storesToWB >= 0);
}

/*template <class Impl>
void
LSQUnit<Impl>::removeMSHR(InstSeqNum seqNum)
{
    list<InstSeqNum>::iterator mshr_it = find(mshrSeqNums.begin(),
                                              mshrSeqNums.end(),
                                              seqNum);

    if (mshr_it != mshrSeqNums.end()) {
        mshrSeqNums.erase(mshr_it);
        DPRINTF(LSQUnit, "Removing MSHR. count = %i\n",mshrSeqNums.size());
    }
}*/

template <class Impl>
void
LSQUnit<Impl>::squash(const InstSeqNum &squashed_num)
{
    DPRINTF(LSQUnit, "Squashing until [sn:%lli]!"
            "(Loads:%i Stores:%i)\n", squashed_num, loads, stores);

    int load_idx = loadTail;
    decrLdIdx(load_idx);

    while (loads != 0 && loadQueue[load_idx]->seqNum > squashed_num) {
        DPRINTF(LSQUnit,"Load Instruction PC %s squashed, "
                "[sn:%lli]\n",
                loadQueue[load_idx]->pcState(),
                loadQueue[load_idx]->seqNum);

        if (isStalled() && load_idx == stallingLoadIdx) {
            stalled = false;
            stallingStoreIsn = 0;
            stallingLoadIdx = 0;
        }

        if (loadQueue[load_idx]->needPostFetch() &&
                loadQueue[load_idx]->readyToExpose() &&
                !loadQueue[load_idx]->isExposeSent()){
            loadsToVLD--;
        }

        // Clear the smart pointer to make sure it is decremented.
        loadQueue[load_idx]->setSquashed();
        loadQueue[load_idx] = NULL;
        --loads;

        // Inefficient!
        loadTail = load_idx;

        decrLdIdx(load_idx);
        ++lsqSquashedLoads;

    }

    if (memDepViolator && squashed_num < memDepViolator->seqNum) {
        memDepViolator = NULL;
    }

    // Jiyong, MLDOM
    if (oblsMissInst && squashed_num < oblsMissInst->seqNum) {
        oblsMissInst = NULL;
    }

    // Jiyong, MLDOM: squash locationPredictor
    if (cpu->enableMLDOM)
        cpu->locationPredictor->squash(squashed_num);

    int store_idx = storeTail;
    decrStIdx(store_idx);

    while (stores != 0 &&
           storeQueue[store_idx].inst->seqNum > squashed_num) {
        // Instructions marked as can WB are already committed.
        if (storeQueue[store_idx].canWB) {
            break;
        }

        DPRINTF(LSQUnit,"Store Instruction PC %s squashed, "
                "idx:%i [sn:%lli]\n",
                storeQueue[store_idx].inst->pcState(),
                store_idx, storeQueue[store_idx].inst->seqNum);

        // I don't think this can happen.  It should have been cleared
        // by the stalling load.
        if (isStalled() &&
            storeQueue[store_idx].inst->seqNum == stallingStoreIsn) {
            panic("Is stalled should have been cleared by stalling load!\n");
            stalled = false;
            stallingStoreIsn = 0;
        }

        // Clear the smart pointer to make sure it is decremented.
        storeQueue[store_idx].inst->setSquashed();
        storeQueue[store_idx].inst = NULL;
        storeQueue[store_idx].canWB = 0;

        // Must delete request now that it wasn't handed off to
        // memory.  This is quite ugly.  @todo: Figure out the proper
        // place to really handle request deletes.
        delete storeQueue[store_idx].req;
        if (TheISA::HasUnalignedMemAcc && storeQueue[store_idx].isSplit) {
            delete storeQueue[store_idx].sreqLow;
            delete storeQueue[store_idx].sreqHigh;

            storeQueue[store_idx].sreqLow = NULL;
            storeQueue[store_idx].sreqHigh = NULL;
        }

        storeQueue[store_idx].req = NULL;
        --stores;

        // Inefficient!
        storeTail = store_idx;

        decrStIdx(store_idx);
        ++lsqSquashedStores;
    }
}


// after sent, we assume the store is complete
// thus, we can wekeup and forward data
// In TSO, mark inFlightStore as true to block following stores [mengjia]
template <class Impl>
void
LSQUnit<Impl>::storePostSend(PacketPtr pkt)
{
    if (isStalled() &&
        storeQueue[storeWBIdx].inst->seqNum == stallingStoreIsn) {
        DPRINTF(LSQUnit, "Unstalling, stalling store [sn:%lli] "
                "load idx:%i\n",
                stallingStoreIsn, stallingLoadIdx);
        stalled = false;
        stallingStoreIsn = 0;
        iewStage->replayMemInst(loadQueue[stallingLoadIdx]);
    }

    if (!storeQueue[storeWBIdx].inst->isStoreConditional()) {
        // The store is basically completed at this time. This
        // only works so long as the checker doesn't try to
        // verify the value in memory for stores.
        storeQueue[storeWBIdx].inst->setCompleted();

        if (cpu->checker) {
            cpu->checker->verify(storeQueue[storeWBIdx].inst);
        }
    }

    if (cpu->needsTSO) {
        storeInFlight = true;
    }

    DPRINTF(LSQUnit, "Post sending store for inst [sn:%lli]\n",
            storeQueue[storeWBIdx].inst->seqNum);
    incrStIdx(storeWBIdx);
}



template <class Impl>
void
LSQUnit<Impl>::completeValidate(DynInstPtr &inst, PacketPtr pkt)
{
    iewStage->wakeCPU();
    // if instruction fault, no need to check value,
    // return directly
    //assert(!inst->needExposeOnly());
    if (inst->isExposeCompleted() || inst->isSquashed()){
        //assert(inst->fault != NoFault);
        //Already sent to commit, do nothing
        return;
    }

    //Check validation result
    bool validation_fail = false;
    if (!cpu->enableMLDOM) {
        if (!inst->isL1HitLow() && inst->vldData[0]==0) {
            validation_fail = true;
        } else {
            if (pkt->isSplit && !inst->isL1HitHigh()
                && inst->vldData[1]==0){
                validation_fail = true;
            }
        }
    }
    else {
        // Jiyong, MLDOM: compare vldData and memData to see if the result is correct
        if (inst->doneWB && inst->doneWB_byOblS) {
            for (int i = 0; i < inst->effSize; i++)
                if (inst->vldData[i] != inst->memData[i])
                    validation_fail = true;
        }
    }

    if (validation_fail){
        // Mark the load for re-execution
        inst->fault = std::make_shared<ReExec>();
        inst->validationFail(true);
        numValidationFails++;
        DPRINTF(LSQUnit, "Validation failed.\n", inst->seqNum);
    }

    inst->setExposeCompleted();
    if ( inst->isExecuted() && inst->isExposeCompleted() ){
        DPRINTF(LSQUnit, "Validation finished. Execution done."
            "Send inst [sn:%lli] to commit stage.\n",
            inst->seqNum);
            //iewStage->instToCommit(inst);
            //iewStage->activityThisCycle();
    } else{
        DPRINTF(LSQUnit, "Validation done. Execution not finishes."
            "Need to wait for readResp for inst [sn:%lli].\n",
            inst->seqNum);
    }
}

template <class Impl>
void
LSQUnit<Impl>::writeback(DynInstPtr &inst, PacketPtr pkt)
{
    iewStage->wakeCPU();

    // Squashed instructions do not need to complete their access.
    if (inst->isSquashed()) {
        assert(!inst->isStore());
        ++lsqIgnoredResponses;
        return;
    }

    DPRINTF(JY, "calling writeback() function for [sn:%lli], packet is final = %d, fromLevel = %d, carryData = %d, isSpec=%d, isExpose=%d, isValidate=%d\n",
            inst->seqNum, pkt->isFinalPacket, pkt->fromLevel, pkt->carryData, pkt->isSpec(), pkt->isExpose(), pkt->isValidate());

    //DPRINTF(LSQUnit, "write back for inst [sn:%lli]\n", inst->seqNum);
    assert(!(inst->isExecuted() && inst->isExposeCompleted() &&
                inst->fault==NoFault) &&
            "in this case, we will put it into ROB twice.");

    if (!inst->isExecuted()) {
        inst->setExecuted();

        if (inst->fault == NoFault) {
            // Complete access to copy data to proper place.
            inst->completeAcc(pkt);
        } else {
            // If the instruction has an outstanding fault, we cannot complete
            // the access as this discards the current fault.

            // If we have an outstanding fault, the fault should only be of
            // type ReExec.
            assert(dynamic_cast<ReExec*>(inst->fault.get()) != nullptr);

            DPRINTF(LSQUnit, "Not completing instruction [sn:%lli] access "
                    "due to pending fault.\n", inst->seqNum);
        }
    }
    DPRINTF(JY, "writeback copy the data to to proper place\n");

    // [mengjia]
    // check schemes to decide whether to set load can be committed
    // on receiving readResp or readSpecResp
    if (!cpu->isInvisibleSpec){
        // if not invisibleSpec mode, we only receive readResp
        assert(!pkt->isSpec() && !pkt->isValidate() &&
                "Receiving spec or validation response "
                "in non invisibleSpec mode");
        iewStage->instToCommit(inst);
    } else if (inst->fault != NoFault){
        //printf("writeback fault value for inst sn:%ld, state=%d\n", inst->seqNum, inst->MLDOM_state);
        inst->setExposeCompleted();
        inst->setExposeSent();
        iewStage->instToCommit(inst);
    } else {
        // cpu->isInvisibleSpec == true
        if (pkt->isSpec()) {
            inst->setSpecCompleted();
        }

        //assert(!pkt->isValidate() && "receiving validation response"
                //"in invisibleSpec RC mode");
        //assert(!pkt->isExpose() && "receiving expose response"
                //"on write back path");

        // check whether the instruction can be committed
        if ( !inst->isExposeCompleted() && inst->needPostFetch() ){
            DPRINTF(LSQUnit, "Expose not finished. "
                "Wait until expose completion"
                " to send inst [sn:%lli] to commit stage\n", inst->seqNum);
        } else {
            DPRINTF(LSQUnit, "Expose and execution both finished. "
                "Send inst [sn:%lli] to commit stage\n", inst->seqNum);
            //iewStage->instToCommit(inst);
        }
        iewStage->instToCommit(inst);
    }

    iewStage->activityThisCycle();

    // see if this load changed the PC
    iewStage->checkMisprediction(inst);
}

// set store to complete [mengjia]
// complete the store after it commits
template <class Impl>
void
LSQUnit<Impl>::completeStore(int store_idx)
{
    assert(storeQueue[store_idx].inst);
    storeQueue[store_idx].completed = true;
    --storesToWB;
    // A bit conservative because a store completion may not free up entries,
    // but hopefully avoids two store completions in one cycle from making
    // the CPU tick twice.
    cpu->wakeCPU();
    cpu->activityThisCycle();

    if (store_idx == storeHead) {
        do {
            incrStIdx(storeHead);

            --stores;
        } while (storeQueue[storeHead].completed &&
                 storeHead != storeTail);

        iewStage->updateLSQNextCycle = true;
    }

    DPRINTF(LSQUnit, "Completing store [sn:%lli], idx:%i, store head "
            "idx:%i\n",
            storeQueue[store_idx].inst->seqNum, store_idx, storeHead);

#if TRACING_ON
    if (DTRACE(O3PipeView)) {
        storeQueue[store_idx].inst->storeTick =
            curTick() - storeQueue[store_idx].inst->fetchTick;
    }
#endif

    if (isStalled() &&
        storeQueue[store_idx].inst->seqNum == stallingStoreIsn) {
        DPRINTF(LSQUnit, "Unstalling, stalling store [sn:%lli] "
                "load idx:%i\n",
                stallingStoreIsn, stallingLoadIdx);
        stalled = false;
        stallingStoreIsn = 0;
        iewStage->replayMemInst(loadQueue[stallingLoadIdx]);
    }

    storeQueue[store_idx].inst->setCompleted();

    if (cpu->needsTSO) {
        storeInFlight = false;
    }

    // Tell the checker we've completed this instruction.  Some stores
    // may get reported twice to the checker, but the checker can
    // handle that case.

    // Store conditionals cannot be sent to the checker yet, they have
    // to update the misc registers first which should take place
    // when they commit
    if (cpu->checker && !storeQueue[store_idx].inst->isStoreConditional()) {
        cpu->checker->verify(storeQueue[store_idx].inst);
    }
}

template <class Impl>
bool
LSQUnit<Impl>::sendStore(PacketPtr data_pkt)
{
    if (!dcachePort->sendTimingReq(data_pkt)) {
        // Need to handle becoming blocked on a store.
        isStoreBlocked = true;
        ++lsqCacheBlocked;
        assert(retryPkt == NULL);
        retryPkt = data_pkt;
        return false;
    }
    setSpecBufState(data_pkt->req);
    return true;
}



template <class Impl>
void
LSQUnit<Impl>::recvRetry()
{
    if (isStoreBlocked) {
        DPRINTF(LSQUnit, "Receiving retry: store blocked\n");
        assert(retryPkt != NULL);
        assert(retryPkt->isWrite());

        LSQSenderState *state =
            dynamic_cast<LSQSenderState *>(retryPkt->senderState);

        if (dcachePort->sendTimingReq(retryPkt)) {
            // Don't finish the store unless this is the last packet.
            if (!TheISA::HasUnalignedMemAcc || !state->pktToSend ||
                    state->pendingPacket == retryPkt) {
                state->pktToSend = false;
                storePostSend(retryPkt);
            }
            retryPkt = NULL;
            isStoreBlocked = false;

            // Send any outstanding packet.
            if (TheISA::HasUnalignedMemAcc && state->pktToSend) {
                assert(state->pendingPacket);
                if (sendStore(state->pendingPacket)) {
                    storePostSend(state->pendingPacket);
                }
            }
        } else {
            // Still blocked!
            ++lsqCacheBlocked;
        }
    }
}

template <class Impl>
inline void
LSQUnit<Impl>::incrStIdx(int &store_idx) const
{
    if (++store_idx >= SQEntries)
        store_idx = 0;
}

template <class Impl>
inline void
LSQUnit<Impl>::decrStIdx(int &store_idx) const
{
    if (--store_idx < 0)
        store_idx += SQEntries;
}

template <class Impl>
inline void
LSQUnit<Impl>::incrLdIdx(int &load_idx) const
{
    if ((++load_idx) >= LQEntries)
        load_idx = 0;
}

template <class Impl>
inline void
LSQUnit<Impl>::decrLdIdx(int &load_idx) const
{
    if ((--load_idx) < 0)
        load_idx += LQEntries;
}

template <class Impl>
void
LSQUnit<Impl>::dumpInsts() const
{
    cprintf("Load store queue: Dumping instructions.\n");
    cprintf("Load queue size: %i\n", loads);
    cprintf("Load queue: ");

    int load_idx = loadHead;

    while (load_idx != loadTail && loadQueue[load_idx]) {
        const DynInstPtr &inst(loadQueue[load_idx]);
        cprintf("%s.[sn:%i] ", inst->pcState(), inst->seqNum);

        incrLdIdx(load_idx);
    }
    cprintf("\n");

    cprintf("Store queue size: %i\n", stores);
    cprintf("Store queue: ");

    int store_idx = storeHead;

    while (store_idx != storeTail && storeQueue[store_idx].inst) {
        const DynInstPtr &inst(storeQueue[store_idx].inst);
        cprintf("%s.[sn:%i] ", inst->pcState(), inst->seqNum);

        incrStIdx(store_idx);
    }

    cprintf("\n");
}

template <class Impl>
void
LSQUnit<Impl>::print_lsq() const {
    int load_idx = loadHead;
    printf("\n LSQ with loadsToVLD = %d, head=%d, tail=%d\n", loadsToVLD, loadHead, loadTail);
    while (load_idx != loadTail && loadQueue[load_idx]) {
        const DynInstPtr &inst(loadQueue[load_idx]);
        printf("idx=%d, [sn:%ld], inst=%s, ", load_idx, inst->seqNum, inst->staticInst->getName().c_str());
        printf("squash=%d, ", inst->isSquashed());
        printf("status=");
        if (inst->isCommitted())
            printf("Committed, ");
        else if (inst->readyToCommit()){
            if (inst->isExecuted())
                printf("CanCommit(Exec), ");
            else
                printf("CanCommit(NonExec), ");
        }
        else if (inst->isExecuted())
            printf("Executed, ");
        else if (inst->isIssued())
            printf("Issued, ");
        else
            printf("Not Issued, ");
        printf("argsTainted=%d, readyToExpose=%d, NeedPostFetch=%d, NeedDeletePostReq=%d, ExposeSent=%d, SpecCompleted=%d\n",
                inst->isArgsTainted(), inst->readyToExpose(), inst->needPostFetch(), inst->needDeletePostReq(), inst->isExposeSent(), inst->isSpecCompleted());

        incrLdIdx(load_idx);
    }
    printf("\n");
}

/*** Jiyong, MLDOM ***/
//  wrapper function for updating the location predictor
template <class Impl>
void
LSQUnit<Impl>::updateLocationPredictor(DynInstPtr &load_inst, int type)
{
    if (!cpu->enableMLDOM)
        return;

    if (load_inst->isSquashed())
        return;

    // no need to update the predictor for static and random and perfect prediction (no predictor)
    assert (load_inst->isLoad());
    if (type == 0) { // comes from a non-spec load
        if (!load_inst->done_locPred_update) {
            load_inst->done_locPred_update = true;

            if (load_inst->regLd_Hit_Level == 0) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L1);

                DPRINTF(JY_SDO_Pred, "<Update> A reg load PC: %s, [sn=%lli] update level=L0\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->regLd_Hit_Level == 1) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L2);

                DPRINTF(JY_SDO_Pred, "<Update> A reg load PC: %s, [sn=%lli] update level=L1\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->regLd_Hit_Level == 2) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L3);

                DPRINTF(JY_SDO_Pred, "<Update> A reg load PC: %s, [sn=%lli] update level=L2\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->regLd_Hit_Level == 3) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   DRAM);

                DPRINTF(JY_SDO_Pred, "<Update> A reg load PC: %s, [sn=%lli] update level=Mem\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else {
                // if all miss, means the data is not local, update L1 by default
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L1);

                DPRINTF(JY_SDO_Pred, "<Update> A reg load PC: %s, [sn=%lli] update nothing (miss)\n",
                        load_inst->pcState(), load_inst->seqNum);
            }

        }
    }
    else if (type == 1) { // comes from a spec load
        if (!load_inst->done_locPred_update) {
            load_inst->done_locPred_update = true;

            if (load_inst->oblS_Hit_L0) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L1);

                DPRINTF(JY_SDO_Pred, "<Update> A obls PC: %s, [sn=%lli] update level=L0\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->oblS_Hit_L1) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L2);

                DPRINTF(JY_SDO_Pred, "<Update> A obls PC: %s, [sn=%lli] update level=L1\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->oblS_Hit_L2) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L3);

                DPRINTF(JY_SDO_Pred, "<Update> A obls PC: %s, [sn=%lli] update level=L2\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->oblS_Hit_Mem) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   DRAM);

                DPRINTF(JY_SDO_Pred, "<Update> A obls PC: %s, [sn=%lli] update level=Mem\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else {
                // if all miss, no update
                load_inst->done_locPred_update = false;
                DPRINTF(JY_SDO_Pred, "<Update> A obls PC: %s, [sn=%lli] update nothing (miss)\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
        }
    }
    else if (type == 2) { // comes from a validate
        if (!load_inst->done_locPred_update) {
            load_inst->done_locPred_update = true;

            if (load_inst->ExpVal_Hit_Level == 0) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L1);

                DPRINTF(JY_SDO_Pred, "<Update> A Val PC: %s, [sn=%lli] update level=L0\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->ExpVal_Hit_Level == 1) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L2);

                DPRINTF(JY_SDO_Pred, "<Update> A Val PC: %s, [sn=%lli] update level=L2\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->ExpVal_Hit_Level == 2) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L3);

                DPRINTF(JY_SDO_Pred, "<Update> A Val PC: %s, [sn=%lli] update level=L3\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else if (load_inst->ExpVal_Hit_Level == 3) {
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   DRAM);

                DPRINTF(JY_SDO_Pred, "<Update> A Val PC: %s, [sn=%lli] update level=Mem\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
            else {
                // if all miss, means the data is not local, update L1 by default
                load_inst->update_locPred_on_commit =
                    cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                                   load_inst->seqNum,
                                                   Cache_L1);

                DPRINTF(JY_SDO_Pred, "<Update> A Val PC: %s, [sn=%lli] update nothing (miss)\n",
                        load_inst->pcState(), load_inst->seqNum);
            }
        }
    }
    else if (type == 3) { // come from store-load forwarding
        if (!load_inst->done_locPred_update) {
            load_inst->done_locPred_update = true;

            load_inst->update_locPred_on_commit =
                cpu->locationPredictor->update(load_inst->pcState().instAddr(),
                                               load_inst->seqNum,
                                               Cache_L1);

            DPRINTF(JY_SDO_Pred, "<Update> A st-ld fwd PC: %s, [sn=%lli] update level=L0\n",
                        load_inst->pcState(), load_inst->seqNum);
        }
    }
    else {
        assert(0);
    }
}

template <class Impl>
typename Impl::DynInstPtr
LSQUnit<Impl>::findProbeLoad(DynInstPtr& load_inst)
{
    int lq_idx = load_inst->lqIdx;
    while (lq_idx != loadHead) {
        DynInstPtr probe_load_inst = loadQueue[lq_idx];
        if (load_inst->pcState().instAddr() == probe_load_inst->pcState().instAddr() &&
            probe_load_inst->pred_level != Cache_L1 &&
            probe_load_inst->chosen_pred_type == Loop)

            return probe_load_inst;

        decrLdIdx(lq_idx);
    }
    return nullptr;
}


#endif//__CPU_O3_LSQ_UNIT_IMPL_HH__
