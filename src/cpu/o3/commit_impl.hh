/*
 * Copyright 2014 Google, Inc.
 * Copyright (c) 2010-2014, 2017 ARM Limited
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
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
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
#ifndef __CPU_O3_COMMIT_IMPL_HH__
#define __CPU_O3_COMMIT_IMPL_HH__

#include <algorithm>
#include <set>
#include <string>

#include "arch/utility.hh"
#include "base/loader/symtab.hh"
#include "base/cp_annotate.hh"
#include "config/the_isa.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/o3/commit.hh"
#include "cpu/o3/thread_state.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "cpu/timebuf.hh"
#include "debug/Activity.hh"
#include "debug/Commit.hh"
#include "debug/CommitRate.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/O3PipeView.hh"
#include "debug/JY.hh"
#include "debug/JY_RET_LD_LAT.hh"
#include "debug/JY_SDO_Pred.hh"
#include "params/DerivO3CPU.hh"
#include "sim/faults.hh"
#include "sim/full_system.hh"

using namespace std;

template <class Impl>
void
DefaultCommit<Impl>::processTrapEvent(ThreadID tid)
{
    // This will get reset by commit if it was switched out at the
    // time of this event processing.
    trapSquash[tid] = true;
}

template <class Impl>
DefaultCommit<Impl>::DefaultCommit(O3CPU *_cpu, DerivO3CPUParams *params)
    : cpu(_cpu),
      iewToCommitDelay(params->iewToCommitDelay),
      commitToIEWDelay(params->commitToIEWDelay),
      renameToROBDelay(params->renameToROBDelay),
      fetchToCommitDelay(params->commitToFetchDelay),
      renameWidth(params->renameWidth),
      commitWidth(params->commitWidth),
      numThreads(params->numThreads),
      drainPending(false),
      drainImminent(false),
      trapLatency(params->trapLatency),
      canHandleInterrupts(true),
      avoidQuiesceLiveLock(false)
{
    if (commitWidth > Impl::MaxWidth)
        fatal("commitWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             commitWidth, static_cast<int>(Impl::MaxWidth));

    _status = Active;
    _nextStatus = Inactive;
    std::string policy = params->smtCommitPolicy;

    //Convert string to lowercase
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   (int(*)(int)) tolower);

    //Assign commit policy
    if (policy == "aggressive"){
        commitPolicy = Aggressive;

        DPRINTF(Commit,"Commit Policy set to Aggressive.\n");
    } else if (policy == "roundrobin"){
        commitPolicy = RoundRobin;

        //Set-Up Priority List
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            priority_list.push_back(tid);
        }

        DPRINTF(Commit,"Commit Policy set to Round Robin.\n");
    } else if (policy == "oldestready"){
        commitPolicy = OldestReady;

        DPRINTF(Commit,"Commit Policy set to Oldest Ready.");
    } else {
        assert(0 && "Invalid SMT Commit Policy. Options Are: {Aggressive,"
               "RoundRobin,OldestReady}");
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        commitStatus[tid] = Idle;
        changedROBNumEntries[tid] = false;
        checkEmptyROB[tid] = false;
        trapInFlight[tid] = false;
        committedStores[tid] = false;
        trapSquash[tid] = false;
        tcSquash[tid] = false;
        pc[tid].set(0);
        lastCommitedSeqNum[tid] = 0;
        squashAfterInst[tid] = NULL;
    }
    lastCommitTick = curTick();
    interrupt = NoFault;
    stalled_counter = 0;

    // Jiyong
    numCommittedInst = 0;
    numCommittedLoad = 0;
}

template <class Impl>
std::string
DefaultCommit<Impl>::name() const
{
    return cpu->name() + ".commit";
}

template <class Impl>
void
DefaultCommit<Impl>::regProbePoints()
{
    ppCommit = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "Commit");
    ppCommitStall = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "CommitStall");
    ppSquash = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "Squash");
}

template <class Impl>
void
DefaultCommit<Impl>::regStats()
{
    using namespace Stats;
    commitSquashedInsts
        .name(name() + ".commitSquashedInsts")
        .desc("The number of squashed insts skipped by commit")
        .prereq(commitSquashedInsts);

    commitNonSpecStalls
        .name(name() + ".commitNonSpecStalls")
        .desc("The number of times commit has been forced to stall to "
              "communicate backwards")
        .prereq(commitNonSpecStalls);

    branchMispredicts
        .name(name() + ".branchMispredicts")
        .desc("The number of times a branch was mispredicted")
        .prereq(branchMispredicts);

    // [Jiyong,STT] stats for STT
    memoryViolations
        .name(name() + ".memoryViolations")
        .desc("The number of times a memory operation(load) has violation (STT)");

    stalledBranchMispredicts
        .name(name() + ".stalledBranchMispredicts")
        .desc("The number of times a branch misprediction is stalled(STT)");

    stalledMemoryViolations
        .name(name() + ".stalledMemoryViolations")
        .desc("The number of times a memory violation is stalled(STT)");

    num_subnormal_fpop
        .name(name() + ".num_subnormal_fpop")
        .desc("The number of committed subnormal fp ops(STT)");

    ExposeValidateBlockStalls
        .name(name() + ".ExposeValidateBlockStalls")
        .desc("The number of ticks when a readyToCommit load at ROB head has its expose/validate blocked (MLLDOM)");

    // [Jiyong,MLDOM] stats for MLDOM
    numExactCorrectPred
        .name(name() + ".numExactCorrectPred")
        .desc("The number of times a spec ld is predicted exactly correctly(MLDOM)");
    numInexactCorrectPred
        .name(name() + ".numInexactCorrectPred")
        .desc("The number of times a spec ld is predcited inexactly correctly(MLDOM)");
    numIncorrectPred
        .name(name() + ".numIncorrectPred")
        .desc("The number of times a spec ld is mispredicted(MLDOM)");
    numL0IsPredicted
        .name(name() + ".numL0IsPredicted")
        .desc("The number of times L0 is predicted for obls of a committed load(MLDOM)");
    numL1IsPredicted
        .name(name() + ".numL1IsPredicted")
        .desc("The number of times L1 is predicted for obls of a committed load(MLDOM)");
    numL2IsPredicted
        .name(name() + ".numL2IsPredicted")
        .desc("The number of times L2 is predicted for obls of a committed load(MLDOM)");
    numMemIsPredicted
        .name(name() + ".numMemIsPredicted")
        .desc("The number of times Mem is predicted for obls of a committed load(MLDOM)");
    numPerfectIsPredicted
        .name(name() + ".numPerfectIsPredicted")
        .desc("The number of times Perfect is predicted for obls for a committed load(MLDOM)");
    numPerfectUnsafeIsPredicted
        .name(name() + ".numPerfectUnsafeIsPredicted")
        .desc("The number of times PerfectUnsafe is predicted for obls for a committed load(MLDOM)");
    numRegL0Hit
        .name(name() + ".numRegL0Hit")
        .desc("The number of times a commit load is regular hit L0(MLDOM)");
    numRegL1Hit
        .name(name() + ".numRegL1Hit")
        .desc("The number of times a commit load is regular hit L1(MLDOM)");
    numRegL2Hit
        .name(name() + ".numRegL2Hit")
        .desc("The number of times a commit load is regular hit L2(MLDOM)");
    numRegMemHit
        .name(name() + ".numRegMemHit")
        .desc("The number of times a commit load is regular hit Mem(MLDOM)");
    numExpValL0Hit
        .name(name() + ".numExpValL0Hit")
        .desc("The number of times a commit load is exp/val hit L0(MLDOM)");
    numExpValL1Hit
        .name(name() + ".numExpValL1Hit")
        .desc("The number of times a commit load is exp/val hit L1(MLDOM)");
    numExpValL2Hit
        .name(name() + ".numExpValL2Hit")
        .desc("The number of times a commit load is exp/val hit L2(MLDOM)");
    numExpValMemHit
        .name(name() + ".numExpValMemHit")
        .desc("The number of times a commit load is exp/val hit Mem(MLDOM)");
    numSpecldL0HitSB
        .name(name() + ".numSpecldL0HitSB")
        .desc("The number of times a commit load used to be obls_L0 and hit Specbuffer(MLDOM)");
    numSpecldL0HitL0
        .name(name() + ".numSpecldL0HitL0")
        .desc("The number of times a commit load used to be obls_L0 and hit L0(MLDOM)");
    numSpecldL1HitSB
        .name(name() + ".numSpecldL1HitSB")
        .desc("The number of times a commit load used to be obls_L1 and hit Specbuffer(MLDOM)");
    numSpecldL1HitL0
        .name(name() + ".numSpecldL1HitL0")
        .desc("The number of times a commit load used to be obls_L1 and hit L0(MLDOM)");
    numSpecldL1HitL1
        .name(name() + ".numSpecldL1HitL1")
        .desc("The number of times a commit load used to be obls_L1 and hit L1(MLDOM)");
    numSpecldL2HitSB
        .name(name() + ".numSpecldL2HitSB")
        .desc("The number of times a commit load used to be obls_L2 and hit Specbuffer(MLDOM)");
    numSpecldL2HitL0
        .name(name() + ".numSpecldL2HitL0")
        .desc("The number of times a commit load used to be obls_L2 and hit L0(MLDOM)");
    numSpecldL2HitL1
        .name(name() + ".numSpecldL2HitL1")
        .desc("The number of times a commit load used to be obls_L2 and hit L1(MLDOM)");
    numSpecldL2HitL2
        .name(name() + ".numSpecldL2HitL2")
        .desc("The number of times a commit load used to be obls_L2 and hit L2(MLDOM)");
    numSpecldMemHitSB
        .name(name() + ".numSpecldMemHitSB")
        .desc("The number of times a commit load used to be obls_Mem and hit Specbuffer(MLDOM)");
    numSpecldMemHitL0
        .name(name() + ".numSpecldMemHitL0")
        .desc("The number of times a commit load used to be obls_Mem and hit L0(MLDOM)");
    numSpecldMemHitL1
        .name(name() + ".numSpecldMemHitL1")
        .desc("The number of times a commit load used to be obls_Mem and hit L1(MLDOM)");
    numSpecldMemHitL2
        .name(name() + ".numSpecldMemHitL2")
        .desc("The number of times a commit load used to be obls_Mem and hit L2(MLDOM)");
    numSpecldMemHitMem
        .name(name() + ".numSpecldMemHitMem")
        .desc("The number of times a commit load used to be obls_Mem and hit Mem(MLDOM)");
    numSpecldPerfectHitSB
        .name(name() + ".numSpecldPerfectHitSB")
        .desc("The number of times a commit load used to be obls_Perfect and hit Specbuffer(MLDOM)");
    numSpecldPerfectHitL0
        .name(name() + ".numSpecldPerfectHitL0")
        .desc("The number of times a commit load used to be obls_Perfect and hit L0(MLDOM)");
    numSpecldPerfectHitL1
        .name(name() + ".numSpecldPerfectHitL1")
        .desc("The number of times a commit load used to be obls_Perfect and hit L1(MLDOM)");
    numSpecldPerfectHitL2
        .name(name() + ".numSpecldPerfectHitL2")
        .desc("The number of times a commit load used to be obls_Perfect and hit L2(MLDOM)");
    numSpecldPerfectHitMem
        .name(name() + ".numSpecldPerfectHitMem")
        .desc("The number of times a commit load used to be obls_Perfect and hit Mem(MLDOM)");
    numSpecldPerfectUnsafeHitSB
        .name(name() + ".numSpecldPerfectUnsafeHitSB")
        .desc("The number of times a commit load used to be obls_PerfectUnsafe and hit Specbuffer(MLDOM)");
    numSpecldPerfectUnsafeHitL0
        .name(name() + ".numSpecldPerfectUnsafeHitL0")
        .desc("The number of times a commit load used to be obls_PerfectUnsafe and hit L0(MLDOM)");
    numSpecldPerfectUnsafeHitL1
        .name(name() + ".numSpecldPerfectUnsafeHitL1")
        .desc("The number of times a commit load used to be obls_PerfectUnsafe and hit L1(MLDOM)");
    numSpecldPerfectUnsafeHitL2
        .name(name() + ".numSpecldPerfectUnsafeHitL2")
        .desc("The number of times a commit load used to be obls_PerfectUnsafe and hit L2(MLDOM)");
    numSpecldPerfectUnsafeHitMem
        .name(name() + ".numSpecldPerfectUnsafeHitMem")
        .desc("The number of times a commit load used to be obls_PerfectUnsafe and hit Mem(MLDOM)");

    numMemLdAliasTaintedNotArrived
        .name(name() + ".numMemLdAliasTaintedNotArrived")
        .desc("The number of times a normal load (hit memory) aliased with obls(MLDOM)");
    numMemLdAliasTaintedArrived
        .name(name() + ".numMemLdAliasTaintedArrived")
        .desc("The number of times a normal load (hit memory) aliased with obls(MLDOM)");
    numMemLdAliasUntaintedNotArrived
        .name(name() + ".numMemLdAliasUntaintedNotArrived")
        .desc("The number of times a normal load (hit memory) aliased with obls(MLDOM)");
    numMemLdAliasUntaintedArrived
        .name(name() + ".numMemLdAliasUntaintedArrived")
        .desc("The number of times a normal load (hit memory) aliased with obls(MLDOM)");

    numMemOblSAliasTaintedNotArrived
        .name(name() + ".numMemOblSAliasTaintedNotArrived")
        .desc("The number of times a obls (hit memory) aliased with obls(MLDOM)");
    numMemOblSAliasTaintedArrived
        .name(name() + ".numMemOblSAliasTaintedArrived")
        .desc("The number of times a obls (hit memory) aliased with obls(MLDOM)");
    numMemOblSAliasUntaintedNotArrived
        .name(name() + ".numMemOblSAliasUntaintedNotArrived")
        .desc("The number of times a obls (hit memory) aliased with obls(MLDOM)");
    numMemOblSAliasUntaintedArrived
        .name(name() + ".numMemOblSAliasUntaintedArrived")
        .desc("The number of times a obls (hit memory) aliased with obls(MLDOM)");

    // [SafeSpec] stat for squash due to invalidation, failed validation
    loadHitInvalidations
        .name(name() + ".loadHitInvalidations")
        .desc("The number of times a load hits a invalidation");
        //.prereq(loadHitInvalidations);

    loadHitExternalEvictions
        .name(name() + ".loadHitExternalEvictions")
        .desc("The number of times a load hits an external invalidation");
        //.prereq(loadHitInvalidations);

    loadValidationFails
        .name(name() + ".loadValidationFails")
        .desc("The number of times a load fails validation");
        //.prereq(loadValidationFails);

    validationStalls
        .name(name() + ".validationStalls")
        .desc("The number of ticks the commit is stalled due to waiting "
                "for validation responses");
        //.prereq(loadValidationFails);

    numCommittedDist
        .init(0,commitWidth,1)
        .name(name() + ".committed_per_cycle")
        .desc("Number of insts commited each cycle")
        .flags(Stats::pdf)
        ;

    instsCommitted
        .init(cpu->numThreads)
        .name(name() + ".committedInsts")
        .desc("Number of instructions committed")
        .flags(total)
        ;

    opsCommitted
        .init(cpu->numThreads)
        .name(name() + ".committedOps")
        .desc("Number of ops (including micro ops) committed")
        .flags(total)
        ;

    statComSwp
        .init(cpu->numThreads)
        .name(name() + ".swp_count")
        .desc("Number of s/w prefetches committed")
        .flags(total)
        ;

    statComRefs
        .init(cpu->numThreads)
        .name(name() +  ".refs")
        .desc("Number of memory references committed")
        .flags(total)
        ;

    statComLoads
        .init(cpu->numThreads)
        .name(name() +  ".loads")
        .desc("Number of loads committed")
        .flags(total)
        ;

    statComMembars
        .init(cpu->numThreads)
        .name(name() +  ".membars")
        .desc("Number of memory barriers committed")
        .flags(total)
        ;

    statComBranches
        .init(cpu->numThreads)
        .name(name() + ".branches")
        .desc("Number of branches committed")
        .flags(total)
        ;

    statComFloating
        .init(cpu->numThreads)
        .name(name() + ".fp_insts")
        .desc("Number of committed floating point instructions.")
        .flags(total)
        ;

    statComVector
        .init(cpu->numThreads)
        .name(name() + ".vec_insts")
        .desc("Number of committed Vector instructions.")
        .flags(total)
        ;

    statComInteger
        .init(cpu->numThreads)
        .name(name()+".int_insts")
        .desc("Number of committed integer instructions.")
        .flags(total)
        ;

    statComFunctionCalls
        .init(cpu->numThreads)
        .name(name()+".function_calls")
        .desc("Number of function calls committed.")
        .flags(total)
        ;

    statCommittedInstType
        .init(numThreads,Enums::Num_OpClass)
        .name(name() + ".op_class")
        .desc("Class of committed instruction")
        .flags(total | pdf | dist)
        ;
    statCommittedInstType.ysubnames(Enums::OpClassStrings);

    commitEligibleSamples
        .name(name() + ".bw_lim_events")
        .desc("number cycles where commit BW limit reached")
        ;
}

template <class Impl>
void
DefaultCommit<Impl>::setThreads(std::vector<Thread *> &threads)
{
    thread = threads;
}

template <class Impl>
void
DefaultCommit<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to send information back to IEW.
    toIEW = timeBuffer->getWire(0);

    // Setup wire to read data from IEW (for the ROB).
    robInfoFromIEW = timeBuffer->getWire(-iewToCommitDelay);
}

template <class Impl>
void
DefaultCommit<Impl>::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    // Setup wire to get instructions from rename (for the ROB).
    fromFetch = fetchQueue->getWire(-fetchToCommitDelay);
}

template <class Impl>
void
DefaultCommit<Impl>::setRenameQueue(TimeBuffer<RenameStruct> *rq_ptr)
{
    renameQueue = rq_ptr;

    // Setup wire to get instructions from rename (for the ROB).
    fromRename = renameQueue->getWire(-renameToROBDelay);
}

template <class Impl>
void
DefaultCommit<Impl>::setIEWQueue(TimeBuffer<IEWStruct> *iq_ptr)
{
    iewQueue = iq_ptr;

    // Setup wire to get instructions from IEW.
    fromIEW = iewQueue->getWire(-iewToCommitDelay);
}

template <class Impl>
void
DefaultCommit<Impl>::setIEWStage(IEW *iew_stage)
{
    iewStage = iew_stage;
}

template<class Impl>
void
DefaultCommit<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

template <class Impl>
void
DefaultCommit<Impl>::setRenameMap(RenameMap rm_ptr[])
{
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid] = &rm_ptr[tid];
}

template <class Impl>
void
DefaultCommit<Impl>::setROB(ROB *rob_ptr)
{
    rob = rob_ptr;
}

template <class Impl>
void
DefaultCommit<Impl>::startupStage()
{
    rob->setActiveThreads(activeThreads);
    rob->resetEntries();

    // Broadcast the number of free entries.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        toIEW->commitInfo[tid].usedROB = true;
        toIEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);
        toIEW->commitInfo[tid].emptyROB = true;
    }

    // Commit must broadcast the number of free entries it has at the
    // start of the simulation, so it starts as active.
    cpu->activateStage(O3CPU::CommitIdx);

    cpu->activityThisCycle();
}

template <class Impl>
void
DefaultCommit<Impl>::drain()
{
    drainPending = true;
}

template <class Impl>
void
DefaultCommit<Impl>::drainResume()
{
    drainPending = false;
    drainImminent = false;
}

template <class Impl>
void
DefaultCommit<Impl>::drainSanityCheck() const
{
    assert(isDrained());
    rob->drainSanityCheck();
}

template <class Impl>
bool
DefaultCommit<Impl>::isDrained() const
{
    /* Make sure no one is executing microcode. There are two reasons
     * for this:
     * - Hardware virtualized CPUs can't switch into the middle of a
     *   microcode sequence.
     * - The current fetch implementation will most likely get very
     *   confused if it tries to start fetching an instruction that
     *   is executing in the middle of a ucode sequence that changes
     *   address mappings. This can happen on for example x86.
     */
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (pc[tid].microPC() != 0)
            return false;
    }

    /* Make sure that all instructions have finished committing before
     * declaring the system as drained. We want the pipeline to be
     * completely empty when we declare the CPU to be drained. This
     * makes debugging easier since CPU handover and restoring from a
     * checkpoint with a different CPU should have the same timing.
     */
    return rob->isEmpty() &&
        interrupt == NoFault;
}

template <class Impl>
void
DefaultCommit<Impl>::takeOverFrom()
{
    _status = Active;
    _nextStatus = Inactive;
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        commitStatus[tid] = Idle;
        changedROBNumEntries[tid] = false;
        trapSquash[tid] = false;
        tcSquash[tid] = false;
        squashAfterInst[tid] = NULL;
    }
    rob->takeOverFrom();
}

template <class Impl>
void
DefaultCommit<Impl>::deactivateThread(ThreadID tid)
{
    list<ThreadID>::iterator thread_it = std::find(priority_list.begin(),
            priority_list.end(), tid);

    if (thread_it != priority_list.end()) {
        priority_list.erase(thread_it);
    }
}


template <class Impl>
void
DefaultCommit<Impl>::updateStatus()
{
    // reset ROB changed variable
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        changedROBNumEntries[tid] = false;

        // Also check if any of the threads has a trap pending
        if (commitStatus[tid] == TrapPending ||
            commitStatus[tid] == FetchTrapPending) {
            _nextStatus = Active;
        }
    }

    if (_nextStatus == Inactive && _status == Active) {
        DPRINTF(Activity, "Deactivating stage.\n");
        cpu->deactivateStage(O3CPU::CommitIdx);
    } else if (_nextStatus == Active && _status == Inactive) {
        DPRINTF(Activity, "Activating stage.\n");
        cpu->activateStage(O3CPU::CommitIdx);
    }

    _status = _nextStatus;
}

template <class Impl>
bool
DefaultCommit<Impl>::changedROBEntries()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (changedROBNumEntries[tid]) {
            return true;
        }
    }

    return false;
}

template <class Impl>
size_t
DefaultCommit<Impl>::numROBFreeEntries(ThreadID tid)
{
    return rob->numFreeEntries(tid);
}

template <class Impl>
void
DefaultCommit<Impl>::generateTrapEvent(ThreadID tid, Fault inst_fault)
{
    DPRINTF(Commit, "Generating trap event for [tid:%i]\n", tid);

    EventFunctionWrapper *trap = new EventFunctionWrapper(
        [this, tid]{ processTrapEvent(tid); },
        "Trap", true, Event::CPU_Tick_Pri);

    Cycles latency = dynamic_pointer_cast<SyscallRetryFault>(inst_fault) ?
                     cpu->syscallRetryLatency : trapLatency;

    cpu->schedule(trap, cpu->clockEdge(latency));
    trapInFlight[tid] = true;
    thread[tid]->trapPending = true;
}

template <class Impl>
void
DefaultCommit<Impl>::generateTCEvent(ThreadID tid)
{
    assert(!trapInFlight[tid]);
    DPRINTF(Commit, "Generating TC squash event for [tid:%i]\n", tid);

    tcSquash[tid] = true;
}

template <class Impl>
void
DefaultCommit<Impl>::squashAll(ThreadID tid)
{
    // If we want to include the squashing instruction in the squash,
    // then use one older sequence number.
    // Hopefully this doesn't mess things up.  Basically I want to squash
    // all instructions of this thread.
    InstSeqNum squashed_inst = rob->isEmpty(tid) ?
        lastCommitedSeqNum[tid] : rob->readHeadInst(tid)->seqNum - 1;

    // All younger instructions will be squashed. Set the sequence
    // number as the youngest instruction in the ROB (0 in this case.
    // Hopefully nothing breaks.)
    youngestSeqNum[tid] = lastCommitedSeqNum[tid];

    rob->squash(squashed_inst, tid);
    changedROBNumEntries[tid] = true;

    // Send back the sequence number of the squashed instruction.
    toIEW->commitInfo[tid].doneSeqNum = squashed_inst;

    // Send back the squash signal to tell stages that they should
    // squash.
    toIEW->commitInfo[tid].squash = true;

    // Send back the rob squashing signal so other stages know that
    // the ROB is in the process of squashing.
    toIEW->commitInfo[tid].robSquashing = true;

    toIEW->commitInfo[tid].mispredictInst = NULL;
    toIEW->commitInfo[tid].squashInst = NULL;

    toIEW->commitInfo[tid].pc = pc[tid];

    //TODO: send a packet to SpecBuffer to indicate flush
    //
}

template <class Impl>
void
DefaultCommit<Impl>::squashFromTrap(ThreadID tid)
{
    squashAll(tid);

    DPRINTF(Commit, "Squashing from trap, restarting at PC %s\n", pc[tid]);

    thread[tid]->trapPending = false;
    thread[tid]->noSquashFromTC = false;
    trapInFlight[tid] = false;

    trapSquash[tid] = false;

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();
}

template <class Impl>
void
DefaultCommit<Impl>::squashFromTC(ThreadID tid)
{
    squashAll(tid);

    DPRINTF(Commit, "Squashing from TC, restarting at PC %s\n", pc[tid]);

    thread[tid]->noSquashFromTC = false;
    assert(!thread[tid]->trapPending);

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();

    tcSquash[tid] = false;
}

template <class Impl>
void
DefaultCommit<Impl>::squashFromSquashAfter(ThreadID tid)
{
    DPRINTF(Commit, "Squashing after squash after request, "
            "restarting at PC %s\n", pc[tid]);

    squashAll(tid);
    // Make sure to inform the fetch stage of which instruction caused
    // the squash. It'll try to re-fetch an instruction executing in
    // microcode unless this is set.
    toIEW->commitInfo[tid].squashInst = squashAfterInst[tid];
    squashAfterInst[tid] = NULL;

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();
}

template <class Impl>
void
DefaultCommit<Impl>::squashAfter(ThreadID tid, DynInstPtr &head_inst)
{
    DPRINTF(Commit, "Executing squash after for [tid:%i] inst [sn:%lli]\n",
            tid, head_inst->seqNum);

    assert(!squashAfterInst[tid] || squashAfterInst[tid] == head_inst);
    commitStatus[tid] = SquashAfterPending;
    squashAfterInst[tid] = head_inst;
}

template <class Impl>
void
DefaultCommit<Impl>::tick()
{
    DPRINTF(Commit, "DefaultCommimt<Impl>::tick() begins\n");
    wroteToTimeBuffer = false;
    _nextStatus = Inactive;

    if (activeThreads->empty())
        return;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    // Check if any of the threads are done squashing.  Change the
    // status if they are done.
    while (threads != end) {
        ThreadID tid = *threads++;

        // Clear the bit saying if the thread has committed stores
        // this cycle.
        committedStores[tid] = false;

        if (commitStatus[tid] == ROBSquashing) {

            if (rob->isDoneSquashing(tid)) {
                commitStatus[tid] = Running;
            } else {
                DPRINTF(Commit,"[tid:%u]: Still Squashing, cannot commit any"
                        " insts this cycle.\n", tid);
                rob->doSquash(tid);
                toIEW->commitInfo[tid].robSquashing = true;
                wroteToTimeBuffer = true;
            }
        }
    }

    commit();

    markCompletedInsts();

    if (cpu->ifPrintROB)
        rob->print_robs();

    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!rob->isEmpty(tid) && rob->readHeadInst(tid)->readyToCommit()) {
            // The ROB has more instructions it can commit. Its next status
            // will be active.
            _nextStatus = Active;

            DynInstPtr inst = rob->readHeadInst(tid);

            DPRINTF(Commit,"[tid:%i]: Instruction [sn:%lli] PC %s is head of"
                    " ROB and ready to commit\n",
                    tid, inst->seqNum, inst->pcState());

            if (inst->ExposeValidateBlocked) {
                DPRINTF(JY, "instruction [sn:%lli] PC %s is head of ROB, but cannot commit since Expose/Validate is blocked\n",
                        inst->seqNum, inst->pcState());
                if (curTick() - lastCommitTick > 0)
                    ExposeValidateBlockStalls += curTick() - lastCommitTick;
            }

        } else if (!rob->isEmpty(tid)) {
            DynInstPtr inst = rob->readHeadInst(tid);

            if (inst->isExecuted() && inst->needPostFetch()
                    && !inst->isExposeCompleted()){
                //stall due to waiting for validation response
                DPRINTF(JY, "Instruction [sn:%lli] PC %s is validation stalled\n",
                        inst->seqNum, inst->pcState());
                if (curTick()-lastCommitTick > 0){
                    validationStalls+= curTick()-lastCommitTick;
                }

            }
            ppCommitStall->notify(inst);

            DPRINTF(Commit,"[tid:%i]: Can't commit, Instruction [sn:%lli] PC "
                    "%s is head of ROB and not ready\n",
                    tid, inst->seqNum, inst->pcState());
        }
        lastCommitTick = curTick();
        DPRINTF(Commit, "[tid:%i]: ROB has %d insts & %d free entries.\n",
                tid, rob->countInsts(tid), rob->numFreeEntries(tid));
    }


    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity This Cycle.\n");
        cpu->activityThisCycle();
    }

    updateStatus();
}

template <class Impl>
void
DefaultCommit<Impl>::handleInterrupt()
{
    // Verify that we still have an interrupt to handle
    if (!cpu->checkInterrupts(cpu->tcBase(0))) {
        DPRINTF(Commit, "Pending interrupt is cleared by master before "
                "it got handled. Restart fetching from the orig path.\n");
        toIEW->commitInfo[0].clearInterrupt = true;
        interrupt = NoFault;
        avoidQuiesceLiveLock = true;
        return;
    }

    // Wait until all in flight instructions are finished before enterring
    // the interrupt.
    if (canHandleInterrupts && cpu->instList.empty()) {
        // Squash or record that I need to squash this cycle if
        // an interrupt needed to be handled.
        DPRINTF(Commit, "Interrupt detected.\n");

        // Clear the interrupt now that it's going to be handled
        toIEW->commitInfo[0].clearInterrupt = true;

        assert(!thread[0]->noSquashFromTC);
        thread[0]->noSquashFromTC = true;

        if (cpu->checker) {
            cpu->checker->handlePendingInt();
        }

        // CPU will handle interrupt. Note that we ignore the local copy of
        // interrupt. This is because the local copy may no longer be the
        // interrupt that the interrupt controller thinks is being handled.
        cpu->processInterrupts(cpu->getInterrupts());

        thread[0]->noSquashFromTC = false;

        commitStatus[0] = TrapPending;

        interrupt = NoFault;

        // Generate trap squash event.
        generateTrapEvent(0, interrupt);

        avoidQuiesceLiveLock = false;
    } else {
        DPRINTF(Commit, "Interrupt pending: instruction is %sin "
                "flight, ROB is %sempty\n",
                canHandleInterrupts ? "not " : "",
                cpu->instList.empty() ? "" : "not " );
    }
}

template <class Impl>
void
DefaultCommit<Impl>::propagateInterrupt()
{
    // Don't propagate intterupts if we are currently handling a trap or
    // in draining and the last observable instruction has been committed.
    if (commitStatus[0] == TrapPending || interrupt || trapSquash[0] ||
            tcSquash[0] || drainImminent)
        return;

    // Process interrupts if interrupts are enabled, not in PAL
    // mode, and no other traps or external squashes are currently
    // pending.
    // @todo: Allow other threads to handle interrupts.

    // Get any interrupt that happened
    interrupt = cpu->getInterrupts();

    // Tell fetch that there is an interrupt pending.  This
    // will make fetch wait until it sees a non PAL-mode PC,
    // at which point it stops fetching instructions.
    if (interrupt != NoFault)
        toIEW->commitInfo[0].interruptPending = true;
}

template <class Impl>
void
DefaultCommit<Impl>::commit()
{
    if (FullSystem) {
        // Check if we have a interrupt and get read to handle it
        if (cpu->checkInterrupts(cpu->tcBase(0)))
            propagateInterrupt();
    }

    ////////////////////////////////////
    // Check for any possible squashes, handle them first
    ////////////////////////////////////
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    int num_squashing_threads = 0;

    while (threads != end) {
        ThreadID tid = *threads++;

        // Not sure which one takes priority.  I think if we have
        // both, that's a bad sign.
        if (trapSquash[tid]) {
            assert(!tcSquash[tid]);
            squashFromTrap(tid);
        } else if (tcSquash[tid]) {
            assert(commitStatus[tid] != TrapPending);
            //TC: thread context. [mengjia]
            squashFromTC(tid);
        } else if (commitStatus[tid] == SquashAfterPending) {
            // A squash from the previous cycle of the commit stage (i.e.,
            // commitInsts() called squashAfter) is pending. Squash the
            // thread now.
            squashFromSquashAfter(tid);
        }

        // Squashed sequence number must be older than youngest valid
        // instruction in the ROB. This prevents squashes from younger
        // instructions overriding squashes from older instructions.
        /****** [Jiyong,STT] Change squash logic ******/
        if (!cpu->STT){   // apply normal squash action if we don't apply STT
            // normal squash handling
            if (fromIEW->squash[tid] &&
                commitStatus[tid] != TrapPending &&
                fromIEW->squashedSeqNum[tid] <= youngestSeqNum[tid]) {

                // we squash with signal from fromIEW
                handleSquashSignalFromIEW(tid);
            }
        } else {    // we apply STT
            if (fromIEW->squash[tid] &&
                commitStatus[tid] != TrapPending &&
                fromIEW->squashedSeqNum[tid] <= youngestSeqNum[tid]) {  // there is a incoming squash signal

                if (!cpu->impChannel) { // Ignore implicit flow
                    handleSquashSignalFromIEW(tid);
                }
                // Note: the following eager scheme is deprecated
                //else if (cpu->configImpFlow == 1) {  // Eager scheme
                    //if (fromIEW->mispredictInst[tid]) { // a branch
                        //// for branch squash, we can handle now, since we are tracking implicit flow
                        //handleSquashSignalFromIEW(tid);
                    //} else {  // a load
                        //// we must delay the load squash if argsTainted
                        //if (fromIEW->instCausingSquash[tid]->isArgsTainted()){
                            //DPRINTF(Commit, "[tid:%i]: (Eager) A load mispredictInst [sn:%lli,0x%lx] PC %s is made pending.\n",
                                    //tid,
                                    //fromIEW->instCausingSquash[tid]->seqNum,
                                    //fromIEW->instCausingSquash[tid]->seqNum,
                                    //fromIEW->instCausingSquash[tid]->pcState());
                            //fromIEW->instCausingSquash[tid]->hasPendingSquash(true);
                            //++stalledMemoryViolations;
                        //} else {
                            //handleSquashSignalFromIEW(tid);
                        //}
                    //}
                //}
                else if (cpu->impChannel){  // apply Lazy scheme
                    // we must delay both branch and load squash if argsTainted
                    if (fromIEW->instCausingSquash[tid]->isArgsTainted()){
                        if (fromIEW->mispredictInst[tid]) {
                            DPRINTF(Commit, "[tid:%i]: (Lazy) A branch mispredicInst [sn:%lli,0x%lx] PC %s is made pending.\n",
                                    tid,
                                    fromIEW->instCausingSquash[tid]->seqNum,
                                    fromIEW->instCausingSquash[tid]->seqNum,
                                    fromIEW->instCausingSquash[tid]->pcState());
                            ++stalledBranchMispredicts;
                        } else {
                            DPRINTF(Commit, "[tid:%i]: (Lazy) A load mispredictInst [sn:%lli,0x%lx] PC %s is made pending.\n", 
                                    tid,
                                    fromIEW->instCausingSquash[tid]->seqNum,
                                    fromIEW->instCausingSquash[tid]->seqNum,
                                    fromIEW->instCausingSquash[tid]->pcState());
                            ++stalledMemoryViolations;
                        }
                        fromIEW->instCausingSquash[tid]->hasPendingSquash(true);
                    } else {
                        handleSquashSignalFromIEW(tid);
                    }
                }
                else {
                    // unknown scheme
                    printf("ERROR: Unspecified implicit flow handling\n");
                    assert(0);
                }
            }
            else {  // there is no squash signal comming
                DynInstPtr resolvedPendingSquashInst = rob->getResolvedPendingSquashInst(tid);
                if (resolvedPendingSquashInst &&
                    commitStatus[tid] != TrapPending &&
                    resolvedPendingSquashInst->seqNum <= youngestSeqNum[tid]){
                    resolvedPendingSquashInst->hasPendingSquash(false);
                    handleSquashSignalFromROB(tid, resolvedPendingSquashInst);
                }
            }
        }

        if (commitStatus[tid] == ROBSquashing) {
            num_squashing_threads++;
        }
    }

    // If commit is currently squashing, then it will have activity for the
    // next cycle. Set its next status as active.
    if (num_squashing_threads) {
        _nextStatus = Active;
    }

    if (num_squashing_threads != numThreads) {
        // If we're not currently squashing, then get instructions.
        getInsts();

        // Try to commit any instructions.
        commitInsts();
    }

    //Check for any activity
    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (changedROBNumEntries[tid]) {
            toIEW->commitInfo[tid].usedROB = true;
            toIEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);

            wroteToTimeBuffer = true;
            changedROBNumEntries[tid] = false;
            if (rob->isEmpty(tid))
                checkEmptyROB[tid] = true;
        }

        // ROB is only considered "empty" for previous stages if: a)
        // ROB is empty, b) there are no outstanding stores, c) IEW
        // stage has received any information regarding stores that
        // committed.
        // c) is checked by making sure to not consider the ROB empty
        // on the same cycle as when stores have been committed.
        // @todo: Make this handle multi-cycle communication between
        // commit and IEW.
        if (checkEmptyROB[tid] && rob->isEmpty(tid) &&
            !iewStage->hasStoresToWB(tid) && !committedStores[tid]) {
            checkEmptyROB[tid] = false;
            toIEW->commitInfo[tid].usedROB = true;
            toIEW->commitInfo[tid].emptyROB = true;
            toIEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);
            wroteToTimeBuffer = true;
        }
    }
}

/*** [Jiyong,STT ***/
template <class Impl>
void
DefaultCommit<Impl>::handleSquashSignalFromIEW(ThreadID tid)
{

    if (fromIEW->mispredictInst[tid]) {
        DPRINTF(Commit, "[tid:%i]: A incoming squash [sn:%lli,0x%lx] PC %s can be resolved now\n",
                tid,
                fromIEW->mispredictInst[tid]->seqNum,
                fromIEW->mispredictInst[tid]->seqNum,
                fromIEW->mispredictInst[tid]->pcState());
        DPRINTF(Commit,
            "[tid:%i]: Squashing due to branch mispred PC:%#x [sn:%i]\n",
            tid,
            fromIEW->mispredictInst[tid]->instAddr(),
            fromIEW->squashedSeqNum[tid]);
    } else {
        DPRINTF(Commit,
            "[tid:%i]: Squashing due to order violation [sn:%i]\n",
            tid, fromIEW->squashedSeqNum[tid]);
    }

    DPRINTF(Commit, "[tid:%i]: Redirecting to PC %#x\n",
            tid,
            fromIEW->pc[tid].nextInstAddr());

    commitStatus[tid] = ROBSquashing;

    // If we want to include the squashing instruction in the squash,
    // then use one older sequence number.
    InstSeqNum squashed_inst = fromIEW->squashedSeqNum[tid];

    if (fromIEW->includeSquashInst[tid]) {
        squashed_inst--;
    }

    // All younger instructions will be squashed. Set the sequence
    // number as the youngest instruction in the ROB.
    youngestSeqNum[tid] = squashed_inst;

    rob->squash(squashed_inst, tid);
    changedROBNumEntries[tid] = true;

    toIEW->commitInfo[tid].doneSeqNum = squashed_inst;

    toIEW->commitInfo[tid].squash = true;

    // Send back the rob squashing signal so other stages know that
    // the ROB is in the process of squashing.
    toIEW->commitInfo[tid].robSquashing = true;

    toIEW->commitInfo[tid].mispredictInst = fromIEW->mispredictInst[tid];
    toIEW->commitInfo[tid].branchTaken = fromIEW->branchTaken[tid];
    toIEW->commitInfo[tid].squashInst = rob->findInst(tid, squashed_inst);

    if (toIEW->commitInfo[tid].mispredictInst) {
        if (toIEW->commitInfo[tid].mispredictInst->isUncondCtrl()) {
             toIEW->commitInfo[tid].branchTaken = true;
        }
        ++branchMispredicts;
    } else {
        ++memoryViolations;
    }

    toIEW->commitInfo[tid].pc = fromIEW->pc[tid];
}

template <class Impl>
void
DefaultCommit<Impl>::handleSquashSignalFromROB(ThreadID tid, DynInstPtr &pendingMispInst)
{
    // only STT has this mode
    assert(cpu->STT);

    DPRINTF(Commit, "[tid:%i]: (Lazy enabled) A pending squash [sn:%lli,0x%lx] PC %s can be resolved now\n",
            tid,
            pendingMispInst->seqNum,
            pendingMispInst->seqNum,
            pendingMispInst->pcState());

    TheISA::PCState nextPC = pendingMispInst->pcState();

    if (pendingMispInst->isControl()) {
        DPRINTF(Commit,
            "[tid:%i]: (Lazy) Squashing due to branch mispred PC:%#x [sn:%i]\n",
            tid,
            pendingMispInst->instAddr(),
            pendingMispInst->seqNum);
        TheISA::advancePC(nextPC, pendingMispInst->staticInst);
    } else if (pendingMispInst->isLoad()){
        DPRINTF(Commit,
            "[tid:%i]: (Lazy) Squashing due to order violation [sn:%i]\n",
            tid, pendingMispInst->seqNum);
    } else {
        assert(0);
    }


    DPRINTF(Commit, "[tid:%i]: (Lazy) Redirecting to PC %#x\n",
            tid,
            nextPC.nextInstAddr());

    commitStatus[tid] = ROBSquashing;

    InstSeqNum squashed_inst = pendingMispInst->seqNum;

    if (pendingMispInst->isLoad())
        squashed_inst--;

    youngestSeqNum[tid] = squashed_inst;

    rob->squash(squashed_inst, tid);
    changedROBNumEntries[tid] = true;

    toIEW->commitInfo[tid].doneSeqNum = squashed_inst;
    toIEW->commitInfo[tid].squash = true;
    toIEW->commitInfo[tid].robSquashing = true;

    if (pendingMispInst->isControl()){
        toIEW->commitInfo[tid].mispredictInst = pendingMispInst;
        toIEW->commitInfo[tid].branchTaken = pendingMispInst->pcState().branching();
    } else
        toIEW->commitInfo[tid].mispredictInst = NULL;

    toIEW->commitInfo[tid].squashInst = rob->findInst(tid, squashed_inst);

    if (toIEW->commitInfo[tid].mispredictInst) {
        if (toIEW->commitInfo[tid].mispredictInst->isUncondCtrl()) {
            toIEW->commitInfo[tid].branchTaken = true;
        }
        ++branchMispredicts;
    } else {
        ++memoryViolations;
    }

    toIEW->commitInfo[tid].pc = nextPC;
}

template <class Impl>
void
DefaultCommit<Impl>::commitInsts()
{
    ////////////////////////////////////
    // Handle commit
    // Note that commit will be handled prior to putting new
    // instructions in the ROB so that the ROB only tries to commit
    // instructions it has in this current cycle, and not instructions
    // it is writing in during this cycle.  Can't commit and squash
    // things at the same time...
    ////////////////////////////////////

    DPRINTF(Commit, "Trying to commit instructions in the ROB.\n");

    unsigned num_committed = 0;

    DynInstPtr head_inst;

    // Commit as many instructions as possible until the commit bandwidth
    // limit is reached, or it becomes impossible to commit any more.
    while (num_committed < commitWidth) {
        // Check for any interrupt that we've already squashed for
        // and start processing it.
        if (interrupt != NoFault)
            handleInterrupt();

        ThreadID commit_thread = getCommittingThread();

        if (commit_thread == -1 || !rob->isHeadReady(commit_thread))
            break;

        head_inst = rob->readHeadInst(commit_thread);

        ThreadID tid = head_inst->threadNumber;

        assert(tid == commit_thread);

        DPRINTF(Commit, "Trying to commit head instruction, [sn:%i] [tid:%i]\n",
                head_inst->seqNum, tid);

        // If the head instruction is squashed, it is ready to retire
        // (be removed from the ROB) at any time.
        if (head_inst->isSquashed()) {

            DPRINTF(Commit, "Retiring squashed instruction from "
                    "ROB.\n");

            rob->retireHead(commit_thread);

            ++commitSquashedInsts;
            // Notify potential listeners that this instruction is squashed
            ppSquash->notify(head_inst);

            // Record that the number of ROB entries has changed.
            changedROBNumEntries[tid] = true;
        } else {
            pc[tid] = head_inst->pcState();

            // Increment the total number of non-speculative instructions
            // executed.
            // Hack for now: it really shouldn't happen until after the
            // commit is deemed to be successful, but this count is needed
            // for syscalls.
            thread[tid]->funcExeInst++;

            // Try to commit the head instruction.
            bool commit_success = commitHead(head_inst, num_committed);

            /*** [Jiyong,STT] sanity check ***/
            if (commit_success)
                stalled_counter = 0;
            else
                stalled_counter++;

            if (stalled_counter == 100000) {
                rob->print_robs();
                assert (0);
            }


            if (commit_success) {
                ++num_committed;
                statCommittedInstType[tid][head_inst->opClass()]++;
                ppCommit->notify(head_inst);

                changedROBNumEntries[tid] = true;

                // Set the doneSeqNum to the youngest committed instruction.
                toIEW->commitInfo[tid].doneSeqNum = head_inst->seqNum;

                if (tid == 0) {
                    //maybe we can use this to mask interrupts [mengjia]
                    canHandleInterrupts =  (!head_inst->isDelayedCommit()) &&
                                           ((THE_ISA != ALPHA_ISA) ||
                                             (!(pc[0].instAddr() & 0x3)));
                }

                // at this point store conditionals should either have
                // been completed or predicated false
                assert(!head_inst->isStoreConditional() ||
                       head_inst->isCompleted() ||
                       !head_inst->readPredicate());

                // Updates misc. registers.
                head_inst->updateMiscRegs();

                // Check instruction execution if it successfully commits and
                // is not carrying a fault.
                if (cpu->checker) {
                    cpu->checker->verify(head_inst);
                }

                cpu->traceFunctions(pc[tid].instAddr());

                TheISA::advancePC(pc[tid], head_inst->staticInst);

                // Keep track of the last sequence number commited
                lastCommitedSeqNum[tid] = head_inst->seqNum;

                // If this is an instruction that doesn't play nicely with
                // others squash everything and restart fetch
                if (head_inst->isSquashAfter())
                    squashAfter(tid, head_inst);

                if (drainPending) {
                    if (pc[tid].microPC() == 0 && interrupt == NoFault &&
                        !thread[tid]->trapPending) {
                        // Last architectually committed instruction.
                        // Squash the pipeline, stall fetch, and use
                        // drainImminent to disable interrupts
                        DPRINTF(Drain, "Draining: %i:%s\n", tid, pc[tid]);
                        squashAfter(tid, head_inst);
                        cpu->commitDrained(tid);
                        drainImminent = true;
                    }
                }

                bool onInstBoundary = !head_inst->isMicroop() ||
                                      head_inst->isLastMicroop() ||
                                      !head_inst->isDelayedCommit();

                if (onInstBoundary) {
                    int count = 0;
                    Addr oldpc;
                    // Make sure we're not currently updating state while
                    // handling PC events.
                    assert(!thread[tid]->noSquashFromTC &&
                           !thread[tid]->trapPending);
                    do {
                        oldpc = pc[tid].instAddr();
                        cpu->system->pcEventQueue.service(thread[tid]->getTC());
                        count++;
                    } while (oldpc != pc[tid].instAddr());
                    if (count > 1) {
                        DPRINTF(Commit,
                                "PC skip function event, stopping commit\n");
                        break;
                    }
                }

                // Check if an instruction just enabled interrupts and we've
                // previously had an interrupt pending that was not handled
                // because interrupts were subsequently disabled before the
                // pipeline reached a place to handle the interrupt. In that
                // case squash now to make sure the interrupt is handled.
                //
                // If we don't do this, we might end up in a live lock situation
                if (!interrupt && avoidQuiesceLiveLock &&
                    onInstBoundary && cpu->checkInterrupts(cpu->tcBase(0)))
                    squashAfter(tid, head_inst);
            } else { // !commit_success
                DPRINTF(Commit, "Unable to commit head instruction PC:%s "
                        "[tid:%i] [sn:%i].\n",
                        head_inst->pcState(), tid ,head_inst->seqNum);
                break;
            }
        }
    }

    DPRINTF(CommitRate, "%i\n", num_committed);
    numCommittedDist.sample(num_committed);

    if (num_committed == commitWidth) {
        commitEligibleSamples++;
    }
}

template <class Impl>
bool
DefaultCommit<Impl>::commitHead(DynInstPtr &head_inst, unsigned inst_num)
{
    assert(head_inst);

    ThreadID tid = head_inst->threadNumber;

    // If the instruction is not executed yet, then it will need extra
    // handling.  Signal backwards that it should be executed.
    if (!head_inst->isExecuted()) {
        // Keep this number correct.  We have not yet actually executed
        // and committed this instruction.
        thread[tid]->funcExeInst--;

        // Make sure we are only trying to commit un-executed instructions we
        // think are possible.
        assert(head_inst->isNonSpeculative() || head_inst->isStoreConditional()
               || head_inst->isMemBarrier() || head_inst->isWriteBarrier() ||
               (head_inst->isLoad() && head_inst->strictlyOrdered()));

        DPRINTF(Commit, "Encountered a barrier or non-speculative "
                "instruction [sn:%lli] at the head of the ROB, PC %s.\n",
                head_inst->seqNum, head_inst->pcState());

        if (inst_num > 0 || iewStage->hasStoresToWB(tid)) {
            DPRINTF(Commit, "Waiting for all stores to writeback.\n");
            return false;
        }

        toIEW->commitInfo[tid].nonSpecSeqNum = head_inst->seqNum;

        // Change the instruction so it won't try to commit again until
        // it is executed.
        head_inst->clearCanCommit();

        if (head_inst->isLoad() && head_inst->strictlyOrdered()) {
            DPRINTF(Commit, "[sn:%lli]: Strictly ordered load, PC %s.\n",
                    head_inst->seqNum, head_inst->pcState());
            toIEW->commitInfo[tid].strictlyOrdered = true;
            toIEW->commitInfo[tid].strictlyOrderedLoad = head_inst;
        } else {
            ++commitNonSpecStalls;
        }

        return false;
    }

    if (head_inst->isThreadSync()) {
        // Not handled for now.
        panic("Thread sync instructions are not handled yet.\n");
    }

    // Check if the instruction caused a fault.  If so, trap.
    Fault inst_fault = head_inst->getFault();

    // Stores mark themselves as completed.
    if (!head_inst->isStore() && inst_fault == NoFault) {
        head_inst->setCompleted();
    }

    if (inst_fault != NoFault) {
        DPRINTF(Commit, "Inst [sn:%lli] PC %s has a fault\n",
                head_inst->seqNum, head_inst->pcState());

        if (iewStage->hasStoresToWB(tid) || inst_num > 0) {
            DPRINTF(Commit, "Stores outstanding, fault must wait.\n");
            return false;
        }

        head_inst->setCompleted();

        // If instruction has faulted, let the checker execute it and
        // check if it sees the same fault and control flow.
        if (cpu->checker) {
            // Need to check the instruction before its fault is processed
            cpu->checker->verify(head_inst);
        }

        assert(!thread[tid]->noSquashFromTC);

        // Mark that we're in state update mode so that the trap's
        // execution doesn't generate extra squashes.
        thread[tid]->noSquashFromTC = true;

        // [SafeSpec] update squash stat for invalidation or validation fails
        updateSquashStats(head_inst);
        // Execute the trap.  Although it's slightly unrealistic in
        // terms of timing (as it doesn't wait for the full timing of
        // the trap event to complete before updating state), it's
        // needed to update the state as soon as possible.  This
        // prevents external agents from changing any specific state
        // that the trap need.
        cpu->trap(inst_fault, tid,
                  head_inst->notAnInst() ?
                      StaticInst::nullStaticInstPtr :
                      head_inst->staticInst);

        // Exit state update mode to avoid accidental updating.
        thread[tid]->noSquashFromTC = false;

        commitStatus[tid] = TrapPending;

        DPRINTF(Commit, "Committing instruction with fault [sn:%lli]\n",
            head_inst->seqNum);
        if (head_inst->traceData) {
            if (DTRACE(ExecFaulting)) {
                head_inst->traceData->setFetchSeq(head_inst->seqNum);
                head_inst->traceData->setCPSeq(thread[tid]->numOp);
                head_inst->traceData->dump();
            }
            delete head_inst->traceData;
            head_inst->traceData = NULL;
        }

        // Generate trap squash event.
        generateTrapEvent(tid, inst_fault);

        return false;
    }

    if (cpu->enableMLDOM && head_inst->isLoad() && !head_inst->readyToExpose()) {
        return false;
    }

    updateComInstStats(head_inst);

    if (FullSystem) {
        if (thread[tid]->profile) {
            thread[tid]->profilePC = head_inst->instAddr();
            ProfileNode *node = thread[tid]->profile->consume(
                    thread[tid]->getTC(), head_inst->staticInst);

            if (node)
                thread[tid]->profileNode = node;
        }
        if (CPA::available()) {
            if (head_inst->isControl()) {
                ThreadContext *tc = thread[tid]->getTC();
                CPA::cpa()->swAutoBegin(tc, head_inst->nextInstAddr());
            }
        }
    }
    /*** Jiyong, check subnormal values for FP operations ***/
    if (head_inst->isFP_transmitter()) {
        for (int i = 0; i < head_inst->numSrcRegs(); i++) {
            if (head_inst->srcRegIdx(i).isFloatReg()) {
                if (cpu->checkFPSubnormal(head_inst->renamedSrcRegIdx(i))) {
                    num_subnormal_fpop++;
                    printf("pc: %lx, inst:%s, ", head_inst->pcState().instAddr(), head_inst->staticInst->disassemble(head_inst->instAddr()).c_str());
                    printf("sn:%ld, inst=%s ", head_inst->seqNum, head_inst->staticInst->getName().c_str());
                    for (int j = 0; j < head_inst->numDestRegs(); j++)
                        printf("%d(%s), ", head_inst->destRegIdx(j).index(), head_inst->destRegIdx(j).className());
                    printf("| ");
                    for (int j = 0; j < head_inst->numSrcRegs(); j++)
                        printf("%d(%s), ", head_inst->srcRegIdx(j).index(), head_inst->srcRegIdx(j).className());
                    printf("| ");

                    for (int j = 0; j < head_inst->numDestRegs(); j++)
                        printf("destPhys[%d] = %d(%d), ", j, head_inst->renamedDestRegIdx(j)->index(), head_inst->renamedDestRegIdx(j)->flatIndex());
                    for (int j = 0; j < head_inst->numSrcRegs(); j++)
                        printf("srcPhys[%d] = %d(%d), ", j, head_inst->renamedSrcRegIdx(j)->index(), head_inst->renamedSrcRegIdx(j)->flatIndex());

                    printf("src idx %d is subnormal\n", i);
                }
            }
        }
    }
    //if (head_inst->macroop)
        //DPRINTF(JY, "Committing instruction %s (macroop %s) with [sn:%lli] PC %s\n",
                //head_inst->staticInst->disassemble(head_inst->instAddr()), head_inst->macroop->disassemble(head_inst->instAddr()),
                //head_inst->seqNum, head_inst->pcState());
    //else // no macroop
        //DPRINTF(JY, "Committing instruction %s (macroop None) with [sn:%lli] PC %s\n",
                //head_inst->staticInst->disassemble(head_inst->instAddr()),
                //head_inst->seqNum, head_inst->pcState());

    //if (head_inst->needPostFetch()) {
        //assert (head_inst->isLoad());
        //DPRINTF(JY, "A load issued SpecGetS is committed. PC: %s ; instr: %s\n",
                //head_inst->pcState(), head_inst->staticInst->disassemble(head_inst->instAddr()));
    //}
    //
    // Jiyong: Print Load stats
    DPRINTF(JY, "Commit #%ld [sn:%lli] instruction now.\n", numCommittedInst, head_inst->seqNum);
    numCommittedInst++;

    // Jiyong: check if the load has updated location predictor
    if (cpu->enableMLDOM) {
        if (head_inst->isLoad() && !head_inst->done_locPred_update) {
            printf("ERROR: load [sn:%ld] has not updated location predictor\n", head_inst->seqNum);
            assert(0);
        }
    }

    /*** Jiyong: print average memory latency ***/
    cpu->print_average_access_latency(numCommittedInst);
    DPRINTF(JY, "print avg latency done \n");

    /*** Jiyong, MLDOM: do commit() for the location predictor ***/
    if (cpu->enableMLDOM && head_inst->isLoad())
        cpu->locationPredictor->commit(head_inst->pcState().instAddr(), head_inst->seqNum, head_inst->update_locPred_on_commit);

    /*** Jiyong, MLDOM: print hit level information about the load ***/
    if (head_inst->isLoad()) {
        Addr M5_VAR_USED cacheBlockMask = ~(64-1);
        if (head_inst->oblS_Hit) {
            int M5_VAR_USED hit_level = -1;
            if (head_inst->oblS_Hit_SB)
                hit_level = -1;
            else if (head_inst->oblS_Hit_L0)
                hit_level = 0;
            else if (head_inst->oblS_Hit_L1)
                hit_level = 1;
            else if (head_inst->oblS_Hit_L2)
                hit_level = 2;
            else if (head_inst->oblS_Hit_Mem)
                hit_level = 3;
            DPRINTF(JY_SDO_Pred, "<Commit> #%ld load(obls) PC:%s, [sn:%lli], lineAddr = 0x%lx, physLineAddr = (0x%lx, 0x%lx), pred_level = %d, pred_type = %d, [obls] hit_level = %d\n",
                    numCommittedLoad, head_inst->pcState(), head_inst->seqNum, head_inst->effAddr & cacheBlockMask, head_inst->physEffAddrLow & cacheBlockMask, head_inst->physEffAddrHigh & cacheBlockMask, head_inst->pred_level, head_inst->chosen_pred_type, hit_level);
        }
        else if (head_inst->ExpVal_Hit_Level >= 0)
            DPRINTF(JY_SDO_Pred, "<Commit> #%ld load(reg) PC:%s, [sn:%lli], lineAddr = 0x%lx, physLineAddr = (0x%lx, 0x%lx) [val] hit_level = %d\n",
                    numCommittedLoad, head_inst->pcState(), head_inst->seqNum, head_inst->effAddr & cacheBlockMask, head_inst->physEffAddrLow & cacheBlockMask, head_inst->physEffAddrHigh & cacheBlockMask, head_inst->ExpVal_Hit_Level);
        else
            DPRINTF(JY_SDO_Pred, "<Commit> #%ld load(reg) PC:%s, [sn:%lli], lineAddr = 0x%lx, physLineAddr = (0x%lx, 0x%lx) [reg] hit_level = %d\n",
                    numCommittedLoad, head_inst->pcState(), head_inst->seqNum, head_inst->effAddr & cacheBlockMask, head_inst->physEffAddrLow & cacheBlockMask, head_inst->physEffAddrHigh & cacheBlockMask, head_inst->regLd_Hit_Level);
        numCommittedLoad++;
    }

    /*** Jiyong, MLDOM: collect location prediction stats ***/
    // location predictor stats
    if (head_inst->isLoad() && head_inst->pred_level != -1) {
        if (head_inst->pred_level == 0) {    // Jiyong: cache level 0
            numL0IsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numExactCorrectPred++;
                numSpecldL0HitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numExactCorrectPred++;
                numSpecldL0HitL0++;
            }
            else
                numIncorrectPred++;
        }
        else if (head_inst->pred_level == 1) {   // Jiyong: cache level 1
            numL1IsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numInexactCorrectPred++;
                numSpecldL1HitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numInexactCorrectPred++;
                numSpecldL1HitL0++;
            }
            else if (head_inst->oblS_Hit_L1) {
                numExactCorrectPred++;
                numSpecldL1HitL1++;
            }
            else
                numIncorrectPred++;
        }
        else if (head_inst->pred_level == 2) {   // Jiyong: cache level 2
            numL2IsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numInexactCorrectPred++;
                numSpecldL2HitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numInexactCorrectPred++;
                numSpecldL2HitL0++;
            }
            else if (head_inst->oblS_Hit_L1) {
                numInexactCorrectPred++;
                numSpecldL2HitL1++;
            }
            else if (head_inst->oblS_Hit_L2) {
                numExactCorrectPred++;
                numSpecldL2HitL2++;
            }
            else
                numIncorrectPred++;
        }
        else if (head_inst->pred_level == 3) {   // Jiyong: cache level 3 (Mem)
            numMemIsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numInexactCorrectPred++;
                numSpecldMemHitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numInexactCorrectPred++;
                numSpecldMemHitL0++;
            }
            else if (head_inst->oblS_Hit_L1) {
                numInexactCorrectPred++;
                numSpecldMemHitL1++;
            }
            else if (head_inst->oblS_Hit_L2) {
                numInexactCorrectPred++;
                numSpecldMemHitL2++;
            }
            else if (head_inst->oblS_Hit_Mem) {
                numExactCorrectPred++;
                numSpecldMemHitMem++;
            }
            else
                numIncorrectPred++;
        }
        else if (head_inst->pred_level == 5) {  // Jiyong: Perfect_Level for perfect predictor
            numPerfectIsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numExactCorrectPred++;
                numSpecldPerfectHitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numExactCorrectPred++;
                numSpecldPerfectHitL0++;
            }
            else if (head_inst->oblS_Hit_L1) {
                numExactCorrectPred++;
                numSpecldPerfectHitL1++;
            }
            else if (head_inst->oblS_Hit_L2) {
                numExactCorrectPred++;
                numSpecldPerfectHitL2++;
            }
            else if (head_inst->oblS_Hit_Mem) {
                numExactCorrectPred++;
                numSpecldPerfectHitMem++;
            }
            else
                numIncorrectPred++;
        }
        else if (head_inst->pred_level == 6) {  // Jiyong: PerfectUnsafe_Level for perfectUnsafe predictor
            numPerfectUnsafeIsPredicted++;
            if (head_inst->oblS_Hit_SB) {
                numExactCorrectPred++;
                numSpecldPerfectUnsafeHitSB++;
            }
            else if (head_inst->oblS_Hit_L0) {
                numExactCorrectPred++;
                numSpecldPerfectUnsafeHitL0++;
            }
            else if (head_inst->oblS_Hit_L1) {
                numExactCorrectPred++;
                numSpecldPerfectUnsafeHitL1++;
            }
            else if (head_inst->oblS_Hit_L2) {
                numExactCorrectPred++;
                numSpecldPerfectUnsafeHitL2++;
            }
            else if (head_inst->oblS_Hit_Mem) {
                numExactCorrectPred++;
                numSpecldPerfectUnsafeHitMem++;
            }
            else
                numIncorrectPred++;
        }
        else {
            printf("ERROR: Unknwon predicted level %d for load instr sn:%ld\n", head_inst->pred_level, head_inst->seqNum);
            assert(0);
        }
    }
    // hit stats
    if (head_inst->isLoad()) {
        if (head_inst->regLd_Hit_Level == 0)
            numRegL0Hit++;
        else if (head_inst->regLd_Hit_Level == 1)
            numRegL1Hit++;
        else if (head_inst->regLd_Hit_Level == 2)
            numRegL2Hit++;
        else if (head_inst->regLd_Hit_Level == 3)
            numRegMemHit++;

        if (head_inst->ExpVal_Hit_Level == 0)
            numExpValL0Hit++;
        else if (head_inst->ExpVal_Hit_Level == 1)
            numExpValL1Hit++;
        else if (head_inst->ExpVal_Hit_Level == 2)
            numExpValL2Hit++;
        else if (head_inst->ExpVal_Hit_Level == 3)
            numExpValMemHit++;
    }

    if (head_inst->isLoad() && head_inst->alias_tainted_not_arrived) {
        if (head_inst->regLd_Hit_Level == 3) {
            DPRINTF(JY, "load [sn:%lli] is committed with load aliased with \n", head_inst->seqNum);
            numMemLdAliasTaintedNotArrived++;
        }
        if (head_inst->oblS_Hit_Mem) {
            DPRINTF(JY, "load [sn:%lli] is committed with obls aliased with \n", head_inst->seqNum);
            numMemOblSAliasTaintedNotArrived++;
        }
    }
    if (head_inst->isLoad() && head_inst->alias_tainted_arrived) {
        if (head_inst->regLd_Hit_Level == 3) {
            DPRINTF(JY, "load [sn:%lli] is committed with load aliased with l\n", head_inst->seqNum);
            numMemLdAliasTaintedArrived++;
        }
        if (head_inst->oblS_Hit_Mem) {
            DPRINTF(JY, "load [sn:%lli] is committed with obls aliased with \n", head_inst->seqNum);
            numMemOblSAliasTaintedArrived++;
        }
    }
    if (head_inst->isLoad() && head_inst->alias_untainted_not_arrived) {
        if (head_inst->regLd_Hit_Level == 3) {
            DPRINTF(JY, "load [sn:%lli] is committed with load aliased with \n", head_inst->seqNum);
            numMemLdAliasUntaintedNotArrived++;
        }
        if (head_inst->oblS_Hit_Mem) {
            DPRINTF(JY, "load [sn:%lli] is committed with obls aliased with \n", head_inst->seqNum);
            numMemOblSAliasUntaintedNotArrived++;
        }
    }
    if (head_inst->isLoad() && head_inst->alias_untainted_arrived) {
        if (head_inst->regLd_Hit_Level == 3) {
            DPRINTF(JY, "load [sn:%lli] is committed with load aliased with \n", head_inst->seqNum);
            numMemLdAliasUntaintedArrived++;
        }
        if (head_inst->oblS_Hit_Mem) {
            DPRINTF(JY, "load [sn:%lli] is committed with obls aliased with \n", head_inst->seqNum);
            numMemOblSAliasUntaintedArrived++;
        }
    }

    // Profiling for loads
    if (head_inst->isLoad()) {
        DPRINTF(JY, "A ld [sn:%lli] is committed at cycle %lld. PC: %s; instr: %s\n",
                head_inst->seqNum, cpu->numCycles.value(), head_inst->pcState(), head_inst->staticInst->disassemble(head_inst->instAddr()));
        DPRINTF(JY_RET_LD_LAT, "A load [sn:%lli] is committed at cycle %lld; PC: %s; PhysAddr: (0x%lx, 0x%lx); delayed_by_STT: %s; delayed_cycle: %d; latency: %d\n",
                head_inst->seqNum,
                cpu->numCycles.value(),
                head_inst->pcState(),
                head_inst->physEffAddrLow,
                head_inst->physEffAddrHigh,
                (head_inst->delayedAtCycle > 0) ? "true" : "false",
                head_inst->delayedCycleCnt,
                head_inst->loadLatency
                );

        uint64_t load_pc = head_inst->pcState().instAddr();

        /*** Jiyong, STT: STT-related profiling ***/
        if (cpu->enable_STT_overhead_profiling) {
            auto load_it1 = cpu->inst_to_num_retired.find(load_pc);
            if (load_it1 == cpu->inst_to_num_retired.end())
                cpu->inst_to_num_retired[load_pc] = 0;
            cpu->inst_to_num_retired[load_pc] += 1;

            auto load_it2 = cpu->inst_to_num_retired_n_delayed.find(load_pc);
            if (load_it2 == cpu->inst_to_num_retired_n_delayed.end())
                cpu->inst_to_num_retired_n_delayed[load_pc] = 0;
            if (head_inst->delayedCycleCnt > 0)
                cpu->inst_to_num_retired_n_delayed[load_pc] += 1;

            auto load_it3 = cpu->inst_to_tot_delayed_cycles.find(load_pc);
            if (load_it3 == cpu->inst_to_tot_delayed_cycles.end())
                cpu->inst_to_tot_delayed_cycles[load_pc] = 0;
            if (head_inst->delayedCycleCnt > 0)
                cpu->inst_to_tot_delayed_cycles[load_pc] += head_inst->delayedCycleCnt;

            cpu->print_STT_overhead_profile(numCommittedInst);
        }

        /*** Jiyong, STT: latency-related profiling ***/
        if (cpu->enable_loadhit_trace_profiling) {
            for (int i = 0; i < cpu->ld_pc_to_profile.size(); i++) {
                if (load_pc == cpu->ld_pc_to_profile[i]) {
                    // this is a load we want to profile
                    auto load_it = cpu->inst_to_hit_trace.find(load_pc);
                    if (load_it == cpu->inst_to_hit_trace.end()) {
                        std::vector<int> new_trace;
                        cpu->inst_to_hit_trace[load_pc] = new_trace;
                    }
                    if (head_inst->if_ldStFwd)
                        cpu->inst_to_hit_trace[load_pc].push_back(0);   // ld-st fwd
                    else if (head_inst->regLd_Hit_Level == 0)
                        cpu->inst_to_hit_trace[load_pc].push_back(1);   // hit L0 (L1 cache)
                    else if (head_inst->regLd_Hit_Level == 1)
                        cpu->inst_to_hit_trace[load_pc].push_back(2);   // hit L1 (L2 cache)
                    else if (head_inst->regLd_Hit_Level == 2)
                        cpu->inst_to_hit_trace[load_pc].push_back(3);   // hit L2 (L3 cache)
                    else if (head_inst->regLd_Hit_Level == 3)
                        cpu->inst_to_hit_trace[load_pc].push_back(4);   // hit L3 (memory)
                    else
                        cpu->inst_to_hit_trace[load_pc].push_back(-1);  // miss all
                }
            }

            cpu->print_loadhit_trace_profile(numCommittedInst);
        }

        //bool print_pc_hit = false;
        //for (int i = 0; i < cpu->ld_pc_to_profile.size(); i++) {
            //if (load_pc == cpu->ld_pc_to_profile[i])
                //print_pc_hit = true;
        //}
        //if (cpu->genHitTraceForLoads > 0 && print_pc_hit) {
            //printf("ld_pc = %ld; PhysAddr: (0x%lx, 0x%lx); delayed_by_STT: %s; delayed_cycle: %ld; latency: %d; ",
                    //load_pc,
                    //head_inst->physEffAddrLow,
                    //head_inst->physEffAddrHigh,
                    //(head_inst->delayedAtCycle > 0) ? "true" : "false",
                    //head_inst->delayedCycleCnt,
                    //head_inst->loadLatency);
        //}

        //auto load_it0 = rob->num_retired.find(load_pc);
        //if (load_it0 == rob->num_retired.end())
            //rob->num_retired[load_pc] = 0;
        //rob->num_retired[load_pc] += 1;

        //if (head_inst->delayedCycleCnt > 0) {
            //auto load_it1 = rob->num_retired_and_delayed.find(load_pc);
            //if (load_it1 == rob->num_retired_and_delayed.end())
                //rob->num_retired_and_delayed[load_pc] = 0;
            //rob->num_retired_and_delayed[load_pc] += 1;

            //auto load_it2 = rob->tot_cycles_delayed_for_delayed_only.find(load_pc);
            //if (load_it2 == rob->tot_cycles_delayed_for_delayed_only.end())
                //rob->tot_cycles_delayed_for_delayed_only[load_pc] = 0;
            //rob->tot_cycles_delayed_for_delayed_only[load_pc] += head_inst->delayedCycleCnt;

            //auto load_it3 = rob->tot_latency_for_delayed_only.find(load_pc);
            //if (load_it3 == rob->tot_latency_for_delayed_only.end())
                //rob->tot_latency_for_delayed_only[load_pc] = 0;
            //rob->tot_latency_for_delayed_only[load_pc] += head_inst->loadLatency;

            //auto load_it4 = rob->hitStatus_for_delayed_only.find(load_pc);
            //if (load_it4 == rob->hitStatus_for_delayed_only.end()) {
                //rob->hitStatus_for_delayed_only[load_pc][0] = 0;
                //rob->hitStatus_for_delayed_only[load_pc][1] = 0;
                //rob->hitStatus_for_delayed_only[load_pc][2] = 0;
                //rob->hitStatus_for_delayed_only[load_pc][3] = 0;
                //rob->hitStatus_for_delayed_only[load_pc][4] = 0;
            //}
            //if (head_inst->if_ldStFwd)
                //rob->hitStatus_for_delayed_only[load_pc][0] += 1;
            //if (head_inst->visibleLd_Hit_Level == 0)
                //rob->hitStatus_for_delayed_only[load_pc][1] += 1;
            //else if (head_inst->visibleLd_Hit_Level == 1)
                //rob->hitStatus_for_delayed_only[load_pc][2] += 1;
            //else if (head_inst->visibleLd_Hit_Level == 2)
                //rob->hitStatus_for_delayed_only[load_pc][3] += 1;
            //else if (head_inst->visibleLd_Hit_Level == 3)
                //rob->hitStatus_for_delayed_only[load_pc][4] += 1;
            //else {
                //DPRINTF(JY, "load inst [sn:%lli] doesn't hit at any level!\n", head_inst->seqNum);
                //assert(0);
            //}
        //}

    }
    else {
        DPRINTF(JY, "A non-ld [sn:%lli] is committed at cycle %lld. PC: %s; instr: %s\n",
                head_inst->seqNum, cpu->numCycles.value(), head_inst->pcState(), head_inst->staticInst->disassemble(head_inst->instAddr()));
    }

    if (head_inst->traceData) {
        head_inst->traceData->setFetchSeq(head_inst->seqNum);
        head_inst->traceData->setCPSeq(thread[tid]->numOp);
        head_inst->traceData->dump();
        delete head_inst->traceData;
        head_inst->traceData = NULL;
    }
    if (head_inst->isReturn()) {
        DPRINTF(Commit,"Return Instruction Committed [sn:%lli] PC %s \n",
                        head_inst->seqNum, head_inst->pcState());
    }

    // Update the commit rename map
    for (int i = 0; i < head_inst->numDestRegs(); i++) {
        renameMap[tid]->setEntry(head_inst->flattenedDestRegIdx(i),
                                 head_inst->renamedDestRegIdx(i));
    }

    // Finally clear the head ROB entry.
    rob->retireHead(tid);

#if TRACING_ON
    if (DTRACE(O3PipeView)) {
        head_inst->commitTick = curTick() - head_inst->fetchTick;
    }
#endif

    // If this was a store, record it for this cycle.
    if (head_inst->isStore())
        committedStores[tid] = true;

    // Return true to indicate that we have committed an instruction.
    return true;
}

template <class Impl>
void
DefaultCommit<Impl>::getInsts()
{
    DPRINTF(Commit, "Getting instructions from Rename stage.\n");

    // Read any renamed instructions and place them into the ROB.
    int insts_to_process = std::min((int)renameWidth, fromRename->size);

    for (int inst_num = 0; inst_num < insts_to_process; ++inst_num) {
        DynInstPtr inst;

        inst = fromRename->insts[inst_num];
        ThreadID tid = inst->threadNumber;

        if (!inst->isSquashed() &&
            commitStatus[tid] != ROBSquashing &&
            commitStatus[tid] != TrapPending) {
            changedROBNumEntries[tid] = true;

            DPRINTF(Commit, "Inserting PC %s [sn:%i] [tid:%i] into ROB.\n",
                    inst->pcState(), inst->seqNum, tid);

            rob->insertInst(inst);

            assert(rob->getThreadEntries(tid) <= rob->getMaxEntries(tid));

            youngestSeqNum[tid] = inst->seqNum;
        } else {
            DPRINTF(Commit, "Instruction PC %s [sn:%i] [tid:%i] was "
                    "squashed, skipping.\n",
                    inst->pcState(), inst->seqNum, tid);
        }
    }
}

template <class Impl>
void
DefaultCommit<Impl>::markCompletedInsts()
{
    // Grab completed insts out of the IEW instruction queue, and mark
    // instructions completed within the ROB.
    for (int inst_num = 0; inst_num < fromIEW->size; ++inst_num) {
        DPRINTF(Commit, "get the inst [num:%d]\n", inst_num);
        assert(fromIEW->insts[inst_num]);
        if (!fromIEW->insts[inst_num]->isSquashed()) {
            DPRINTF(Commit, "[tid:%i]: Marking PC %s, [sn:%lli] ready "
                    "within ROB.\n",
                    fromIEW->insts[inst_num]->threadNumber,
                    fromIEW->insts[inst_num]->pcState(),
                    fromIEW->insts[inst_num]->seqNum);

            // Mark the instruction as ready to commit.
            fromIEW->insts[inst_num]->setCanCommit();
        }
    }

    // [SafeSpec]
    // update load status
    // isPrevInstsCompleted; isPrevBrsResolved
    rob->updateVisibleState();

    // [Jiyong,STT]
    // taint/untaint rob instructions
    if (cpu->STT)
        rob->compute_taint();


    // [Jiyong,STT]
    // if rob head is a tainted transmitter, increment the counter
    //rob->critical_loads_delay();
}

// [SafeSpec] update squash stat for loads
template <class Impl>
void
DefaultCommit<Impl>::updateSquashStats(DynInstPtr &inst)
{
    if (inst->hitInvalidation()){
        loadHitInvalidations++;
    }
    if (inst->validationFail()){
        loadValidationFails++;
    }
    if (inst->hitExternalEviction()){
        loadHitExternalEvictions++;
    }
}

template <class Impl>
void
DefaultCommit<Impl>::updateComInstStats(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    if (!inst->isMicroop() || inst->isLastMicroop())
        instsCommitted[tid]++;
    opsCommitted[tid]++;

    // To match the old model, don't count nops and instruction
    // prefetches towards the total commit count.
    if (!inst->isNop() && !inst->isInstPrefetch()) {
        cpu->instDone(tid, inst);
    }

    //
    //  Control Instructions
    //
    if (inst->isControl())
        statComBranches[tid]++;

    //
    //  Memory references
    //
    if (inst->isMemRef()) {
        statComRefs[tid]++;

        if (inst->isLoad()) {
            statComLoads[tid]++;
        }
    }

    if (inst->isMemBarrier()) {
        statComMembars[tid]++;
    }

    // Integer Instruction
    if (inst->isInteger())
        statComInteger[tid]++;

    // Floating Point Instruction
    if (inst->isFloating())
        statComFloating[tid]++;
    // Vector Instruction
    if (inst->isVector())
        statComVector[tid]++;

    // Function Calls
    if (inst->isCall())
        statComFunctionCalls[tid]++;

}

////////////////////////////////////////
//                                    //
//  SMT COMMIT POLICY MAINTAINED HERE //
//                                    //
////////////////////////////////////////
template <class Impl>
ThreadID
DefaultCommit<Impl>::getCommittingThread()
{
    if (numThreads > 1) {
        switch (commitPolicy) {

          case Aggressive:
            //If Policy is Aggressive, commit will call
            //this function multiple times per
            //cycle
            return oldestReady();

          case RoundRobin:
            return roundRobin();

          case OldestReady:
            return oldestReady();

          default:
            return InvalidThreadID;
        }
    } else {
        assert(!activeThreads->empty());
        ThreadID tid = activeThreads->front();

        if (commitStatus[tid] == Running ||
            commitStatus[tid] == Idle ||
            commitStatus[tid] == FetchTrapPending) {
            return tid;
        } else {
            return InvalidThreadID;
        }
    }
}

template<class Impl>
ThreadID
DefaultCommit<Impl>::roundRobin()
{
    list<ThreadID>::iterator pri_iter = priority_list.begin();
    list<ThreadID>::iterator end      = priority_list.end();

    while (pri_iter != end) {
        ThreadID tid = *pri_iter;

        if (commitStatus[tid] == Running ||
            commitStatus[tid] == Idle ||
            commitStatus[tid] == FetchTrapPending) {

            if (rob->isHeadReady(tid)) {
                priority_list.erase(pri_iter);
                priority_list.push_back(tid);

                return tid;
            }
        }

        pri_iter++;
    }

    return InvalidThreadID;
}

template<class Impl>
ThreadID
DefaultCommit<Impl>::oldestReady()
{
    unsigned oldest = 0;
    bool first = true;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!rob->isEmpty(tid) &&
            (commitStatus[tid] == Running ||
             commitStatus[tid] == Idle ||
             commitStatus[tid] == FetchTrapPending)) {

            if (rob->isHeadReady(tid)) {

                DynInstPtr head_inst = rob->readHeadInst(tid);

                if (first) {
                    oldest = tid;
                    first = false;
                } else if (head_inst->seqNum < oldest) {
                    oldest = tid;
                }
            }
        }
    }

    if (!first) {
        return oldest;
    } else {
        return InvalidThreadID;
    }
}

#endif//__CPU_O3_COMMIT_IMPL_HH__
