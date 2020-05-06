/*
 * Copyright (c) 2011 ARM Limited
 * All rights reserved.
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
 */

#ifndef __CPU_BASE_DYN_INST_IMPL_HH__
#define __CPU_BASE_DYN_INST_IMPL_HH__

#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "base/cprintf.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "cpu/base_dyn_inst.hh"
#include "cpu/exetrace.hh"
#include "cpu/op_class.hh"
#include "debug/DynInst.hh"
#include "debug/IQ.hh"
#include "mem/request.hh"
#include "sim/faults.hh"

template <class Impl>
BaseDynInst<Impl>::BaseDynInst(const StaticInstPtr &_staticInst,
                               const StaticInstPtr &_macroop,
                               TheISA::PCState _pc, TheISA::PCState _predPC,
                               InstSeqNum seq_num, ImplCPU *cpu)
  : staticInst(_staticInst), cpu(cpu), traceData(NULL), macroop(_macroop)
{
    seqNum = seq_num;

    pc = _pc;
    predPC = _predPC;

    initVars();
}

template <class Impl>
BaseDynInst<Impl>::BaseDynInst(const StaticInstPtr &_staticInst,
                               const StaticInstPtr &_macroop)
    : staticInst(_staticInst), traceData(NULL), macroop(_macroop)
{
    seqNum = 0;
    initVars();
}

template <class Impl>
void
BaseDynInst<Impl>::initVars()
{
    memData = NULL;
    vldData = NULL;
    stFwdData = NULL;
    stFwdDataSize = 0;
    effAddr = 0;
    physEffAddrLow = 0;
    physEffAddrHigh = 0;
    readyRegs = 0;
    memReqFlags = 0;

    status.reset();

    instFlags.reset();
    instFlags[RecordResult] = true;
    instFlags[Predicate] = true;
    /*** [Jiyong,STT] ***/
    taint_prop_done = false;
    instFlags[HasPendingSquash] = false;
    issueCycle = 0;
    delayedCycleCnt = 0;
    delayedAtCycle = -1;
    loadLatency = 0;
    criticalDelay = 0;
    stLdFwd_delayed = false;

    robUpdateVisibleState = false;
    lsqUpdateVisibleState = false;
    /*** [Jiyong,MLDOM] ***/
    MLDOM_state = -1;
    if_ldStFwd = false;
    regLd_Hit_Level = -1;
    ExpVal_Hit_Level = -1;

    doneWB        = false;  // set when a WB is done (spec load only)
    doneWB_byOblS = false;  // set when the WB is done by a OblS (spec load only)

    pred_level = -1;
    chosen_pred_type = -1;
    perfect_oblS = 0;

    oblS_Sent = false;
    oblS_Complete = false;
    oblS_Hit      = false;
    oblS_Hit_SB   = false;
    oblS_Hit_L0   = false;
    oblS_Hit_L1   = false;
    oblS_Hit_L2   = false;
    oblS_Hit_Mem  = false;

    oblS_FstHalfComplete = false;
    oblS_FstHalfHit      = false;
    oblS_FstHalfHit_SB   = false;
    oblS_FstHalfHit_L0   = false;
    oblS_FstHalfHit_L1   = false;
    oblS_FstHalfHit_L2   = false;
    oblS_FstHalfHit_Mem  = false;

    oblS_SndHalfComplete = false;
    oblS_SndHalfHit      = false;
    oblS_SndHalfHit_SB   = false;
    oblS_SndHalfHit_L0   = false;
    oblS_SndHalfHit_L1   = false;
    oblS_SndHalfHit_L2   = false;
    oblS_SndHalfHit_Mem  = false;

    pkt_delayed_wb = nullptr;
    valPred_onMiss = false;
    done_locPred_update = false;
    update_locPred_on_commit = 0;
    obls_TLB_Hit = true;    // only false cause trouble

    needSingleExpose    = false;
    needSingleValidate  = false;
    needDoubleExpose    = false;
    needDoubleValidate  = false;
    ExposeValidateBlocked = false;

    alias_tainted_not_arrived = false;
    alias_tainted_arrived = false;
    alias_untainted_not_arrived = false;
    alias_untainted_arrived = false;

    endByFault = false;

    lqIdx = -1;
    sqIdx = -1;

    // Eventually make this a parameter.
    threadNumber = 0;

    // Also make this a parameter, or perhaps get it from xc or cpu.
    asid = 0;

    // Initialize the fault to be NoFault.
    fault = NoFault;

#ifndef NDEBUG
    ++cpu->instcount;

    if (cpu->instcount > 1500) {
#ifdef DEBUG
        //cpu->dumpInsts();
        //dumpSNList();
#endif
        //cpu->print_robs();
        //assert(cpu->instcount <= 1500);
    }

    DPRINTF(DynInst,
        "DynInst: [sn:%lli] Instruction created. Instcount for %s = %i\n",
        seqNum, cpu->name(), cpu->instcount);
#endif

#ifdef DEBUG
    cpu->snList.insert(seqNum);
#endif

    reqToVerify = NULL;
    postReq = NULL;
    postSreqLow = NULL;
    postSreqHigh = NULL;

    // [Jiyong,STT] set argProducers to nullptr
    for(int i = 0; i < TheISA::MaxInstSrcRegs; i++)
        argProducers[i] = DynInstPtr();
}

template <class Impl>
BaseDynInst<Impl>::~BaseDynInst()
{
    if (memData) {
        delete [] memData;
    }

    if (vldData) {
        delete [] vldData;
    }

    if (stFwdData) {
        delete [] stFwdData;
    }

    if (traceData) {
        delete traceData;
    }

    fault = NoFault;

#ifndef NDEBUG
    --cpu->instcount;

    DPRINTF(DynInst,
        "DynInst: [sn:%lli] Instruction destroyed. Instcount for %s = %i\n",
        seqNum, cpu->name(), cpu->instcount);
#endif
#ifdef DEBUG
    cpu->snList.erase(seqNum);
#endif

    if (reqToVerify)
        delete reqToVerify;

    if (needDeletePostReq()){
        if (postReq){
            delete postReq;
            postReq = NULL;
        }
        if (postSreqLow) {
            delete postSreqLow;
            delete postSreqHigh;
            postSreqLow = NULL;
            postSreqHigh = NULL;
        }
    }
}

#ifdef DEBUG
template <class Impl>
void
BaseDynInst<Impl>::dumpSNList()
{
    std::set<InstSeqNum>::iterator sn_it = cpu->snList.begin();

    int count = 0;
    while (sn_it != cpu->snList.end()) {
        cprintf("%i: [sn:%lli] not destroyed\n", count, (*sn_it));
        count++;
        sn_it++;
    }
}
#endif

template <class Impl>
void
BaseDynInst<Impl>::dump()
{
    cprintf("T%d : %#08d `", threadNumber, pc.instAddr());
    std::cout << staticInst->disassemble(pc.instAddr());
    cprintf("'\n");
}

template <class Impl>
void
BaseDynInst<Impl>::dump(std::string &outstring)
{
    std::ostringstream s;
    s << "T" << threadNumber << " : 0x" << pc.instAddr() << " "
      << staticInst->disassemble(pc.instAddr());

    outstring = s.str();
}

template <class Impl>
void
BaseDynInst<Impl>::markSrcRegReady()
{
    DPRINTF(IQ, "[sn:%lli] has %d ready out of %d sources. RTI %d)\n",
            seqNum, readyRegs+1, numSrcRegs(), readyToIssue());
    if (++readyRegs == numSrcRegs()) {
        setCanIssue();
    }
}

template <class Impl>
void
BaseDynInst<Impl>::markSrcRegReady(RegIndex src_idx)
{
    _readySrcRegIdx[src_idx] = true;

    markSrcRegReady();
}

template <class Impl>
bool
BaseDynInst<Impl>::eaSrcsReady()
{
    // For now I am assuming that src registers 1..n-1 are the ones that the
    // EA calc depends on.  (i.e. src reg 0 is the source of the data to be
    // stored)

    for (int i = 1; i < numSrcRegs(); ++i) {
        if (!_readySrcRegIdx[i])
            return false;
    }

    return true;
}

// Jiyong, STT: for other transmitter types
template <class Impl>
bool
BaseDynInst<Impl>::readyToIssue_UT() const
{
    bool ret = status[CanIssue];
    if (cpu->moreTransTypes == 1) {
        // consider int div and fp div
        if (isFP_transmitter())
            ret = ret && (!instFlags[IsArgsTainted]);
    }
    else if (cpu->moreTransTypes == 2) {
        if (isInt_transmitter())
            ret = ret && (!instFlags[IsArgsTainted]);
    }
    else {
        assert (0);
    }
    return ret;
}

template <class Impl>
bool
BaseDynInst<Impl>::isFP_transmitter() const
{
    if (isFloating()) {
        if (opClass() == FloatMultOp ||
            opClass() == FloatMultAccOp ||
            opClass() == FloatDivOp ||
            opClass() == FloatSqrtOp ||
            opClass() == SimdFloatMultOp ||
            opClass() == SimdFloatMultAccOp ||
            opClass() == SimdFloatDivOp ||
            opClass() == SimdFloatSqrtOp)
            return true;
        else
            return false;
    }
    else
        return false;
}
template <class Impl>
bool
BaseDynInst<Impl>::isInt_transmitter() const
{
    if (isInteger()) {
        if (opClass() == IntMultOp ||
            opClass() == IntDivOp)
            return true;
        else
            return false;
    }
    else
        return false;
}

#endif//__CPU_BASE_DYN_INST_IMPL_HH__
