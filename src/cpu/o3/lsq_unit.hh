/*
 * Copyright (c) 2012-2014,2017 ARM Limited
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
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
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

#ifndef __CPU_O3_LSQ_UNIT_HH__
#define __CPU_O3_LSQ_UNIT_HH__

#include <algorithm>
#include <cstring>
#include <map>
#include <queue>

#include "arch/generic/debugfaults.hh"
#include "arch/isa_traits.hh"
#include "arch/locked_mem.hh"
#include "arch/mmapped_ipr.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"
#include "cpu/timebuf.hh"
#include "cpu/o3/locPred.hh"
#include "debug/LSQUnit.hh"
#include "debug/JY.hh"
#include "debug/JY_SDO_Pred.hh"
#include "mem/packet.hh"
#include "mem/port.hh"

struct DerivO3CPUParams;

/**
 * Class that implements the actual LQ and SQ for each specific
 * thread.  Both are circular queues; load entries are freed upon
 * committing, while store entries are freed once they writeback. The
 * LSQUnit tracks if there are memory ordering violations, and also
 * detects partial load to store forwarding cases (a store only has
 * part of a load's data) that requires the load to wait until the
 * store writes back. In the former case it holds onto the instruction
 * until the dependence unit looks at it, and in the latter it stalls
 * the LSQ until the store writes back. At that point the load is
 * replayed.
 */
template <class Impl>
class LSQUnit {
  public:
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::CPUPol::IEW IEW;
    typedef typename Impl::CPUPol::LSQ LSQ;
    typedef typename Impl::CPUPol::IssueStruct IssueStruct;

  public:
    /** Constructs an LSQ unit. init() must be called prior to use. */
    LSQUnit();

    /** Initializes the LSQ unit with the specified number of entries. */
    void init(O3CPU *cpu_ptr, IEW *iew_ptr, DerivO3CPUParams *params,
            LSQ *lsq_ptr, unsigned maxLQEntries, unsigned maxSQEntries,
            unsigned id);

    /** Returns the name of the LSQ unit. */
    std::string name() const;

    /** Registers statistics. */
    void regStats();

    /** Sets the pointer to the dcache port. */
    void setDcachePort(MasterPort *dcache_port);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Ticks the LSQ unit, which in this case only resets the number of
     * used cache ports.
     * @todo: Move the number of used ports up to the LSQ level so it can
     * be shared by all LSQ units.
     */
    void tick() { usedStorePorts = 0; }

    /** Inserts an instruction. */
    void insert(DynInstPtr &inst);
    /** Inserts a load instruction. */
    void insertLoad(DynInstPtr &load_inst);
    /** Inserts a store instruction. */
    void insertStore(DynInstPtr &store_inst);

    /** Check for ordering violations in the LSQ. For a store squash if we
     * ever find a conflicting load. For a load, only squash if we
     * an external snoop invalidate has been seen for that load address
     * @param load_idx index to start checking at
     * @param inst the instruction to check
     */
    Fault checkViolations(int load_idx, DynInstPtr &inst);

    /** Check if an incoming invalidate hits in the lsq on a load
     * that might have issued out of order wrt another load beacuse
     * of the intermediate invalidate.
     */
    void checkSnoop(PacketPtr pkt);

    // [SafeSpec] check whether current request will hit in the
    // spec buffer or not
    int checkSpecBufHit(const RequestPtr req, const int req_idx);
    void setSpecBufState(const RequestPtr req);

    bool checkPrevLoadsExecuted(const int req_idx);
    /** Executes a load instruction. */
    Fault executeLoad(DynInstPtr &inst);

    Fault executeLoad(int lq_idx) { panic("Not implemented"); return NoFault; }
    /** Executes a store instruction. */
    Fault executeStore(DynInstPtr &inst);

    /** Commits the head load. */
    void commitLoad();
    /** Commits loads older than a specific sequence number. */
    void commitLoads(InstSeqNum &youngest_inst);

    /** Commits stores older than a specific sequence number. */
    void commitStores(InstSeqNum &youngest_inst);

    /** Writes back stores. */
    void writebackStores();

    /** [mengjia] Validate loads. */
    int exposeLoads();

    /** [mengjia] Update Visbible State.
     * In the mode defence relying on fence: setup fenceDelay state.
     * In the mode defence relying on invisibleSpec:
     * setup readyToExpose*/
    void updateVisibleState();

    /** Completes the data access that has been returned from the
     * memory system. */
    void completeDataAccess(PacketPtr pkt);

    /** Clears all the entries in the LQ. */
    void clearLQ();

    /** Clears all the entries in the SQ. */
    void clearSQ();

    /** Resizes the LQ to a given size. */
    void resizeLQ(unsigned size);

    /** Resizes the SQ to a given size. */
    void resizeSQ(unsigned size);

    /** Squashes all instructions younger than a specific sequence number. */
    void squash(const InstSeqNum &squashed_num);

    /** Returns if there is a memory ordering violation. Value is reset upon
     * call to getMemDepViolator().
     */
    bool violation() { return memDepViolator; }

    /** Jiyong, MLDOM: returns if there is a obls missing squash. Value is reset
     * upon call to getOblsMissInst().
     */
    bool oblsSquash() { return oblsMissInst; }

    /** Returns the memory ordering violator. */
    DynInstPtr getMemDepViolator();

    // Jiyong, MLDOM
    /** gets the instruction that suffers from squash caused by obls miss */
    DynInstPtr getOblsMissInst();

    DynInstPtr getWithoutDeleteOblsMissInst() { return oblsMissInst; }

    /** Returns the number of free LQ entries. */
    unsigned numFreeLoadEntries();

    /** Returns the number of free SQ entries. */
    unsigned numFreeStoreEntries();

    /** Returns the number of loads in the LQ. */
    int numLoads() { return loads; }

    /** Returns the number of stores in the SQ. */
    int numStores() { return stores; }

    /** Returns if either the LQ or SQ is full. */
    bool isFull() { return lqFull() || sqFull(); }

    /** Returns if both the LQ and SQ are empty. */
    bool isEmpty() const { return lqEmpty() && sqEmpty(); }

    /** Returns if the LQ is full. */
    bool lqFull() { return loads >= (LQEntries - 1); }

    /** Returns if the SQ is full. */
    bool sqFull() { return stores >= (SQEntries - 1); }

    /** Returns if the LQ is empty. */
    bool lqEmpty() const { return loads == 0; }

    /** Returns if the SQ is empty. */
    bool sqEmpty() const { return stores == 0; }

    /** Returns the number of instructions in the LSQ. */
    unsigned getCount() { return loads + stores; }

    /** Returns if there are any stores to writeback. */
    bool hasStoresToWB() { return storesToWB; }

    /** Returns the number of stores to writeback. */
    int numStoresToWB() { return storesToWB; }
    /** [SafeSpec] Returns the number of loads to validate. */
    int numLoadsToVLD() { return loadsToVLD; }

    /** Returns if the LSQ unit will writeback on this cycle. */
    bool willWB() { return storeQueue[storeWBIdx].canWB &&
                        !storeQueue[storeWBIdx].completed &&
                        !isStoreBlocked; }

    /** Handles doing the retry. */
    void recvRetry();

  private:
    /** Reset the LSQ state */
    void resetState();

    /** Writes back the instruction, sending it to IEW. */
    void writeback(DynInstPtr &inst, PacketPtr pkt);

    // [SafeSpec] complete Validates
    void completeValidate(DynInstPtr &inst, PacketPtr pkt);

    /** Writes back a store that couldn't be completed the previous cycle. */
    void writebackPendingStore();

    /** Handles completing the send of a store to memory. */
    void storePostSend(PacketPtr pkt);

    /** Handles completing the send of a validation to memory. */
    //void validationPostSend(PacketPtr pkt, int loadVLDIdx);

    /** Completes the store at the specified index. */
    void completeStore(int store_idx);

    /** Attempts to send a store to the cache. */
    bool sendStore(PacketPtr data_pkt);

    /** Attempts to send a validation to the cache. */
    //bool sendValidation(PacketPtr data_pkt, int loadVLDIdx);

    /** Increments the given store index (circular queue). */
    inline void incrStIdx(int &store_idx) const;
    /** Decrements the given store index (circular queue). */
    inline void decrStIdx(int &store_idx) const;
    /** Increments the given load index (circular queue). */
    inline void incrLdIdx(int &load_idx) const;
    /** Decrements the given load index (circular queue). */
    inline void decrLdIdx(int &load_idx) const;

    // Jiyong, MLDOM
    /** Find the older load which is predicted "taken" by loop predictor **/
    DynInstPtr findProbeLoad(DynInstPtr &load_inst);

  public:
    /** Debugging function to dump instructions in the LSQ. */
    void dumpInsts() const;

  private:
    /** Pointer to the CPU. */
    O3CPU *cpu;

    /** Pointer to the IEW stage. */
    IEW *iewStage;

    /** Pointer to the LSQ. */
    LSQ *lsq;

    /** Pointer to the dcache port.  Used only for sending. */
    MasterPort *dcachePort;

    /** Derived class to hold any sender state the LSQ needs. */
    class LSQSenderState : public Packet::SenderState
    {
      public:
        /** Default constructor. */
        LSQSenderState()
            : mainPkt(NULL), pendingPacket(NULL), idx(0), outstanding(1),
              isLoad(false), noWB(false), isSplit(false),
              pktToSend(false), cacheBlocked(false)
          { }

        /** Instruction who initiated the access to memory. */
        DynInstPtr inst;
        /** The main packet from a split load, used during writeback. */
        PacketPtr mainPkt;
        /** A second packet from a split store that needs sending. */
        PacketPtr pendingPacket;
        /** The LQ/SQ index of the instruction. */
        uint8_t idx;
        /** Number of outstanding packets to complete. */
        uint8_t outstanding;
        /** Whether or not it is a load. */
        bool isLoad;
        /** Whether or not the instruction will need to writeback. */
        bool noWB;
        /** Whether or not this access is split in two. */
        bool isSplit;
        /** Whether or not there is a packet that needs sending. */
        bool pktToSend;
        /** Whether or not the second packet of this split load was blocked */
        bool cacheBlocked;

        /** Completes a packet and returns whether the access is finished. */
        inline bool complete() { return --outstanding == 0; }
    };


    /** Writeback event, specifically for when stores forward data to loads. */
    class WritebackEvent : public Event {
      public:
        /** Constructs a writeback event. */
        WritebackEvent(DynInstPtr &_inst, PacketPtr pkt, LSQUnit *lsq_ptr);

        /** Processes the writeback event. */
        void process();

        /** Returns the description of this event. */
        const char *description() const;

      private:
        /** Instruction whose results are being written back. */
        DynInstPtr inst;

        /** The packet that would have been sent to memory. */
        PacketPtr pkt;

        /** The pointer to the LSQ unit that issued the store. */
        LSQUnit<Impl> *lsqPtr;
    };

  public:
    struct SQEntry {
        /** Constructs an empty store queue entry. */
        SQEntry()
            : inst(NULL), req(NULL), size(0),
              canWB(0), committed(0), completed(0)
        {
            std::memset(data, 0, sizeof(data));
        }

        ~SQEntry()
        {
            inst = NULL;
        }

        /** Constructs a store queue entry for a given instruction. */
        SQEntry(DynInstPtr &_inst)
            : inst(_inst), req(NULL), sreqLow(NULL), sreqHigh(NULL), size(0),
              isSplit(0), canWB(0), committed(0), completed(0), isAllZeros(0)
        {
            std::memset(data, 0, sizeof(data));
        }
        /** The store data. */
        char data[16];
        /** The store instruction. */
        DynInstPtr inst;
        /** The request for the store. */
        RequestPtr req;
        /** The split requests for the store. */
        RequestPtr sreqLow;
        RequestPtr sreqHigh;
        /** The size of the store. */
        uint8_t size;
        /** Whether or not the store is split into two requests. */
        bool isSplit;
        /** Whether or not the store can writeback. */
        bool canWB;
        /** Whether or not the store is committed. */
        bool committed;
        /** Whether or not the store is completed. */
        bool completed;
        /** Does this request write all zeros and thus doesn't
         * have any data attached to it. Used for cache block zero
         * style instructs (ARM DC ZVA; ALPHA WH64)
         */
        bool isAllZeros;
    };

  private:
    /** The LSQUnit thread id. */
    ThreadID lsqID;

    /** The store queue. */
    std::vector<SQEntry> storeQueue;

    /** The load queue. */
    std::vector<DynInstPtr> loadQueue;

    /** The number of LQ entries, plus a sentinel entry (circular queue).
     *  @todo: Consider having var that records the true number of LQ entries.
     */
    unsigned LQEntries;
    /** The number of SQ entries, plus a sentinel entry (circular queue).
     *  @todo: Consider having var that records the true number of SQ entries.
     */
    unsigned SQEntries;

    /** The number of places to shift addresses in the LSQ before checking
     * for dependency violations
     */
    unsigned depCheckShift;

    /** Should loads be checked for dependency issues */
    bool checkLoads;

    /** The number of load instructions in the LQ. */
    int loads;
    /** [mengjia] The number of store instructions in the SQ waiting to writeback. */
    int loadsToVLD;
    /** The number of store instructions in the SQ. */
    int stores;
    /** The number of store instructions in the SQ waiting to writeback. */
    int storesToWB;

    /** The index of the head instruction in the LQ. */
    int loadHead;
    /** [mengjia] The index of the first instruction that may be ready to be
     * validated, and has not yet been validated.
     */
    //int pendingLoadVLDIdx;
    /** The index of the tail instruction in the LQ. */
    int loadTail;

    /** The index of the head instruction in the SQ. */
    int storeHead;
    /** The index of the first instruction that may be ready to be
     * written back, and has not yet been written back.
     */
    int storeWBIdx;
    /** The index of the tail instruction in the SQ. */
    int storeTail;

    /// @todo Consider moving to a more advanced model with write vs read ports
    /** The number of cache ports available each cycle (stores only). */
    int cacheStorePorts;

    /** [SafeSpec] The number of used cache ports in this cycle by stores. */
    int usedStorePorts;

    //list<InstSeqNum> mshrSeqNums;

    /** Address Mask for a cache block (e.g. ~(cache_block_size-1)) */
    Addr cacheBlockMask;

    /** Wire to read information from the issue stage time queue. */
    typename TimeBuffer<IssueStruct>::wire fromIssue;

    /** Whether or not the LSQ is stalled. */
    bool stalled;
    /** The store that causes the stall due to partial store to load
     * forwarding.
     */
    InstSeqNum stallingStoreIsn;
    /** The index of the above store. */
    int stallingLoadIdx;

    /** The packet that needs to be retried. */
    PacketPtr retryPkt;

    /** Whehter or not a store is blocked due to the memory system. */
    bool isStoreBlocked;

    /** Whehter or not a validation is blocked due to the memory system. */
    bool isValidationBlocked;
    
    /** Whether or not a store is in flight. */
    bool storeInFlight;

    /** The oldest load that caused a memory ordering violation. */
    DynInstPtr memDepViolator;

    // Jiyong, MLDOM
    /** The oldest load that caused a obls miss squash */
    DynInstPtr oblsMissInst;

    /** Whether or not there is a packet that couldn't be sent because of
     * a lack of cache ports. */
    bool hasPendingPkt;

    /** The packet that is pending free cache ports. */
    PacketPtr pendingPkt;

    // Will also need how many read/write ports the Dcache has.  Or keep track
    // of that in stage that is one level up, and only call executeLoad/Store
    // the appropriate number of times.
    /** Total number of loads forwaded from LSQ stores. */
    Stats::Scalar lsqForwLoads;
    Stats::Scalar taintedlsqForwLoads;

    /** Total number of loads ignored due to invalid addresses. */
    Stats::Scalar invAddrLoads;

    /** Total number of squashed loads. */
    Stats::Scalar lsqSquashedLoads;

    /** Total number of responses from the memory system that are
     * ignored due to the instruction already being squashed. */
    Stats::Scalar lsqIgnoredResponses;

    /** Tota number of memory ordering violations. */
    Stats::Scalar lsqMemOrderViolation;

    /** Total number of squashed stores. */
    Stats::Scalar lsqSquashedStores;

    /** Total number of software prefetches ignored due to invalid addresses. */
    Stats::Scalar invAddrSwpfs;

    /** Ready loads blocked due to partial store-forwarding. */
    Stats::Scalar lsqBlockedLoads;

    /** Number of loads that were rescheduled. */
    Stats::Scalar lsqRescheduledLoads;

    /** Number of times the LSQ is blocked due to the cache. */
    Stats::Scalar lsqCacheBlocked;

    /** Number of times the LSQ is cache blocked by expose/validate */
    Stats::Scalar lsqCacheBlockedByExpVal;

    Stats::Scalar specBufHits;
    Stats::Scalar specBufMisses;
    Stats::Scalar numValidates;
    Stats::Scalar numExposes;
    Stats::Scalar numConvertedExposes;
    Stats::Scalar removedExposes;  // Jiyong, MLDOM

    /*** [Jiyong,STT] ***/
    /** Total number of loads have load-load issue with an older tainted load.*/
    Stats::Scalar taintedSnoopLoadViolation;
    std::map<uint64_t, uint64_t> delay_count;

    /*** [Jiyong, MLDOM] ***/
    Stats::Scalar numNonSpecLdSent;
    Stats::Scalar numSpecLdL0Sent;
    Stats::Scalar numSpecLdL1Sent;
    Stats::Scalar numSpecLdL2Sent;
    Stats::Scalar numSpecLdMemSent;
    Stats::Scalar numSpecLdPerfectSent;
    Stats::Scalar numSpecLdPerfectUnsafeSent;
    Stats::Scalar numSquashOnMiss_SpecLdL0;             // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnMiss_SpecLdL1;             // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnMiss_SpecLdL2;             // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnMiss_SpecLdMem;            // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnMiss_SpecLdPerfect;        // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnMiss_SpecLdPerfectUnsafe;  // in Exposeloads(), squash on AB(Miss)
    Stats::Scalar numSquashOnSpecTLBMiss;               // in Exposeloads(), squash on spec TLB miss
    Stats::Scalar numValidationFails;

    Stats::Scalar num_ABmiss;
    Stats::Scalar num_Ab;

    Stats::Scalar numSpecLdL0_hitSB;
    Stats::Scalar numSpecLdL0_hitL0;
    Stats::Scalar numSpecLdL0_miss;

    Stats::Scalar numSpecLdL1_hitSB;
    Stats::Scalar numSpecLdL1_hitL0;
    Stats::Scalar numSpecLdL1_hitL1;
    Stats::Scalar numSpecLdL1_miss;

    Stats::Scalar numSpecLdL2_hitSB;
    Stats::Scalar numSpecLdL2_hitL0;
    Stats::Scalar numSpecLdL2_hitL1;
    Stats::Scalar numSpecLdL2_hitL2;
    Stats::Scalar numSpecLdL2_miss;

    Stats::Scalar numSpecLdMem_hitSB;
    Stats::Scalar numSpecLdMem_hitL0;
    Stats::Scalar numSpecLdMem_hitL1;
    Stats::Scalar numSpecLdMem_hitL2;
    Stats::Scalar numSpecLdMem_hitMem;
    Stats::Scalar numSpecLdMem_miss;

    Stats::Scalar numSpecLdPerfect_hitSB;
    Stats::Scalar numSpecLdPerfect_hitL0;
    Stats::Scalar numSpecLdPerfect_hitL1;
    Stats::Scalar numSpecLdPerfect_hitL2;
    Stats::Scalar numSpecLdPerfect_hitMem;
    Stats::Scalar numSpecLdPerfect_miss;

    Stats::Scalar numSpecLdPerfectUnsafe_hitSB;
    Stats::Scalar numSpecLdPerfectUnsafe_hitL0;
    Stats::Scalar numSpecLdPerfectUnsafe_hitL1;
    Stats::Scalar numSpecLdPerfectUnsafe_hitL2;
    Stats::Scalar numSpecLdPerfectUnsafe_hitMem;
    Stats::Scalar numSpecLdPerfectUnsafe_miss;


  public:
    void print_lsq() const;

    /** Executes the load at the given index. */
    Fault read(Request *req, Request *sreqLow, Request *sreqHigh,
               int load_idx);

    /** Executes the store at the given index. */
    Fault write(Request *req, Request *sreqLow, Request *sreqHigh,
                uint8_t *data, int store_idx);

    /** Returns the index of the head load instruction. */
    int getLoadHead() { return loadHead; }
    /** Returns the sequence number of the head load instruction. */
    InstSeqNum getLoadHeadSeqNum()
    {
        if (loadQueue[loadHead]) {
            return loadQueue[loadHead]->seqNum;
        } else {
            return 0;
        }

    }

    /** Returns the index of the head store instruction. */
    int getStoreHead() { return storeHead; }
    /** Returns the sequence number of the head store instruction. */
    InstSeqNum getStoreHeadSeqNum()
    {
        if (storeQueue[storeHead].inst) {
            return storeQueue[storeHead].inst->seqNum;
        } else {
            return 0;
        }

    }

    /** Returns whether or not the LSQ unit is stalled. */
    bool isStalled()  { return stalled; }

    /*** [Jiyong, MLDOM] ***/
    enum MLDOM_STATE_Type {
        // basic scheme without hit optimization (each ld only has 1 response)
        MLDOM_STATE_A    = 0,
        MLDOM_STATE_AB   = 1,
        MLDOM_STATE_ABC  = 2,
        MLDOM_STATE_ABCD = 3,
        MLDOM_STATE_AC   = 4,
        MLDOM_STATE_ACB  = 5,
        MLDOM_STATE_ACBD = 6,
        MLDOM_STATE_ACD  = 7,
        MLDOM_STATE_ACDB = 8,
        // extra state introduced by hit optimization (each ld has multiple response)
        MLDOM_STATE_Ab    = 9,  // receive a early hit
        MLDOM_STATE_AbC   = 10, // receive a early hit, ld becomes safe
        MLDOM_STATE_AbCB  = 11, // receive a early hit, ld becomes safe, receive OblS complete
        MLDOM_STATE_AbCBD = 12, // receive a early hit, ld becomes safe, receive OblS complete, Exp/Val complete
        MLDOM_STATE_AbCD  = 13, // receive a early hit, ld becomes safe, Exp/Val complete,
        MLDOM_STATE_AbCDB = 14, // receive a early hit, ld becomes safe, Exp/Val complete, receive OblS complete
        MLDOM_STATE_ACb   = 15, // ld becomes safe, receive a early hit
        MLDOM_STATE_ACbD  = 16, // ld becomes safe, receive a early hit, Exp/Val complete
        MLDOM_STATE_ACbDB = 17, // ld becomes safe, receive a early hit, Exp/Val complete, receive OblS complete
        MLDOM_STATE_toBeSquash = 18
    };

    void updateLocationPredictor(DynInstPtr &load_inst, int type);
};


// IMPORTANT: the function to issue packets, interact with memory [mengjia]
template <class Impl>
Fault
LSQUnit<Impl>::read(Request *req, Request *sreqLow, Request *sreqHigh,
                    int load_idx)
{
    DynInstPtr load_inst = loadQueue[load_idx];

    assert(load_inst);

    // Make sure this isn't a strictly ordered load
    // A bit of a hackish way to get strictly ordered accesses to work
    // only if they're at the head of the LSQ and are ready to commit
    // (at the head of the ROB too).
    if (req->isStrictlyOrdered() &&
        (load_idx != loadHead || !load_inst->isAtCommit())) {
        iewStage->rescheduleMemInst(load_inst);
        ++lsqRescheduledLoads;
        DPRINTF(LSQUnit, "Strictly ordered load [sn:%lli] PC %s\n",
                load_inst->seqNum, load_inst->pcState());

        // Must delete request now that it wasn't handed off to
        // memory.  This is quite ugly.  @todo: Figure out the proper
        // place to really handle request deletes.
        delete req;
        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            delete sreqLow;
            delete sreqHigh;
        }
        return std::make_shared<GenericISA::M5PanicFault>(
            "Strictly ordered load [sn:%llx] PC %s\n",
            load_inst->seqNum, load_inst->pcState());
    }

    // Check the SQ for any previous stores that might lead to forwarding
    // why we have store queue index for a load operation? [mengjia]
    // load_inst->sqIdx is the youngest store in store queue when load is inserted in load queue
    int store_idx = load_inst->sqIdx;

    int store_size = 0;

    DPRINTF(LSQUnit, "Read called, load idx: %i, store idx: %i, "
            "storeHead: %i addr: %#x%s\n",
            load_idx, store_idx, storeHead, req->getPaddr(),
            sreqLow ? " split" : "");

    // LLSC: load-link/store-conditional [mengjia]
    if (req->isLLSC()) {
        assert(!sreqLow);
        // Disable recording the result temporarily.  Writing to misc
        // regs normally updates the result, but this is not the
        // desired behavior when handling store conditionals.
        load_inst->recordResult(false);
        TheISA::handleLockedRead(load_inst.get(), req);
        load_inst->recordResult(true);
    }

    // request to memory mapped register [mengjia]
    if (req->isMmappedIpr()) {
        assert(!load_inst->memData);
        load_inst->memData = new uint8_t[64];

        ThreadContext *thread = cpu->tcBase(lsqID);
        Cycles delay(0);

        PacketPtr data_pkt = new Packet(req, MemCmd::ReadReq);

        data_pkt->dataStatic(load_inst->memData);
        if (!TheISA::HasUnalignedMemAcc || !sreqLow) {
            delay = TheISA::handleIprRead(thread, data_pkt);
        } else {
            assert(sreqLow->isMmappedIpr() && sreqHigh->isMmappedIpr());
            PacketPtr fst_data_pkt = new Packet(sreqLow, MemCmd::ReadReq);
            PacketPtr snd_data_pkt = new Packet(sreqHigh, MemCmd::ReadReq);

            fst_data_pkt->dataStatic(load_inst->memData);
            snd_data_pkt->dataStatic(load_inst->memData + sreqLow->getSize());

            delay = TheISA::handleIprRead(thread, fst_data_pkt);
            Cycles delay2 = TheISA::handleIprRead(thread, snd_data_pkt);
            if (delay2 > delay)
                delay = delay2;

            delete sreqLow;
            delete sreqHigh;
            delete fst_data_pkt;
            delete snd_data_pkt;
        }
        WritebackEvent *wb = new WritebackEvent(load_inst, data_pkt, this);
        cpu->schedule(wb, cpu->clockEdge(delay));
        return NoFault;
    }

    // Here is store-load forwarding logic
    while (store_idx != -1) {
        // End once we've reached the top of the LSQ
        if (store_idx == storeWBIdx) {
            break;
        }

        // Move the index to one younger
        if (--store_idx < 0)
            store_idx += SQEntries;

        assert(storeQueue[store_idx].inst);

        store_size = storeQueue[store_idx].size;

        if (!store_size || storeQueue[store_idx].inst->strictlyOrdered() ||
            (storeQueue[store_idx].req &&
             storeQueue[store_idx].req->isCacheMaintenance())) {
            // Cache maintenance instructions go down via the store
            // path but they carry no data and they shouldn't be
            // considered for forwarding
            continue;
        }

        assert(storeQueue[store_idx].inst->effAddrValid());

        // Check if the store data is within the lower and upper bounds of
        // addresses that the request needs.
        bool store_has_lower_limit =
            req->getVaddr() >= storeQueue[store_idx].inst->effAddr;
        bool store_has_upper_limit =
            (req->getVaddr() + req->getSize()) <=
            (storeQueue[store_idx].inst->effAddr + store_size);
        bool lower_load_has_store_part =
            req->getVaddr() < (storeQueue[store_idx].inst->effAddr +
                           store_size);
        bool upper_load_has_store_part =
            (req->getVaddr() + req->getSize()) >
            storeQueue[store_idx].inst->effAddr;

        // If the store's data has all of the data needed and the load isn't
        // LLSC, we can forward.
        if (store_has_lower_limit && store_has_upper_limit && !req->isLLSC()) {
            int shift_amt = req->getVaddr() - storeQueue[store_idx].inst->effAddr;

            // Allocate memory if this is the first time a load is issued.
            if (!load_inst->memData) {
                load_inst->memData = new uint8_t[req->getSize()];
            }
            if (storeQueue[store_idx].isAllZeros)
                memset(load_inst->memData, 0, req->getSize());
            else
                memcpy(load_inst->memData,
                    storeQueue[store_idx].data + shift_amt, req->getSize());

            PacketPtr data_pkt = new Packet(req, MemCmd::ReadReq);
            data_pkt->dataStatic(load_inst->memData);

            WritebackEvent *wb = new WritebackEvent(load_inst, data_pkt, this);

            DPRINTF(LSQUnit, "Forwarding from store idx %i to load[sn:%lli] with address %#x\n", 
                    store_idx, load_inst->seqNum, req->getVaddr());

            // We'll say this has a 1 cycle load-store forwarding latency
            // for now.
            // @todo: Need to make this a parameter.
            cpu->schedule(wb, curTick());
            ++lsqForwLoads;
            load_inst->if_ldStFwd = true;

            //load_inst->alreadyForwarded = true;
            //break;
            // Don't need to do anything special for split loads.
            if (TheISA::HasUnalignedMemAcc && sreqLow) {
                delete sreqLow;
                delete sreqHigh;
            }

            return NoFault;

        } else if (
                (!req->isLLSC() &&
                 ((store_has_lower_limit && lower_load_has_store_part) ||
                  (store_has_upper_limit && upper_load_has_store_part) ||
                  (lower_load_has_store_part && upper_load_has_store_part))) ||
                (req->isLLSC() &&
                 ((store_has_lower_limit || upper_load_has_store_part) &&
                  (store_has_upper_limit || lower_load_has_store_part)))) {
            // This is the partial store-load forwarding case where a store
            // has only part of the load's data and the load isn't LLSC or
            // the load is LLSC and the store has all or part of the load's
            // data

            // If it's already been written back, then don't worry about
            // stalling on it.
            if (storeQueue[store_idx].completed) {
                panic("Should not check one of these");
                continue;
            }

            // Must stall load and force it to retry, so long as it's the oldest
            // load that needs to do so.
            if (!stalled ||
                (stalled &&
                 load_inst->seqNum <
                 loadQueue[stallingLoadIdx]->seqNum)) {
                stalled = true;
                stallingStoreIsn = storeQueue[store_idx].inst->seqNum;
                stallingLoadIdx = load_idx;
            }

            // Tell IQ/mem dep unit that this instruction will need to be
            // rescheduled eventually
            iewStage->rescheduleMemInst(load_inst);
            load_inst->clearIssued();
            ++lsqRescheduledLoads;

            // Do not generate a writeback event as this instruction is not
            // complete.
            DPRINTF(LSQUnit, "Load-store forwarding mis-match. "
                    "Store idx %i to load addr %#x\n",
                    store_idx, req->getVaddr());

            // Must delete request now that it wasn't handed off to
            // memory.  This is quite ugly.  @todo: Figure out the
            // proper place to really handle request deletes.
            delete req;
            if (TheISA::HasUnalignedMemAcc && sreqLow) {
                delete sreqLow;
                delete sreqHigh;
            }

            return NoFault;
        }
    } // load store forwarding check done

    // Allocate memory if this is the first time a load is issued.
    if (!load_inst->memData) {
        load_inst->memData = new uint8_t[req->getSize()];
    }

    // if we the cache is not blocked, do cache access
    bool completedFirst = false;

    PacketPtr data_pkt = NULL;
    PacketPtr fst_data_pkt = NULL;
    PacketPtr snd_data_pkt = NULL;

    // According to the isInsivisibleSpec variable to create
    // corresponding type of packets [mengjia]
    bool sendSpecRead = false;
    if (cpu->isInvisibleSpec) {
        if(!load_inst->readyToExpose()){
            assert(!req->isLLSC());
            assert(!req->isStrictlyOrdered());
            assert(!req->isMmappedIpr());
            sendSpecRead = true;
            DPRINTF(LSQUnit, "send a spec read for inst [sn:%lli]\n",
                    load_inst->seqNum);
        }
    }

    assert( !(sendSpecRead && load_inst->isSpecCompleted()) &&
            "Sending specRead twice for the same load insts");


    // Jiyong, MLDOM: predict the cache level for all loads
    if (cpu->enableMLDOM) {
        // predictor predicts for the load regardless of if it's necessary
        LocPred_t chosen_pred;
        load_inst->pred_level = cpu->locationPredictor->predict(load_inst->pcState().instAddr(), load_inst->seqNum, chosen_pred);
        load_inst->chosen_pred_type = chosen_pred;
    }

    /* solution 1: stall the load */
    // If the load is predicted by the loop and "not taken", it can be stalled by a pending "taken"
    if (sendSpecRead && load_inst->chosen_pred_type == Loop && load_inst->pred_level == Cache_L1) {
        DPRINTF(JY, "load [sn=%lli] is predicted as L1 by loop\n", load_inst->seqNum);
        DynInstPtr probe_inst = findProbeLoad(load_inst);

        if (probe_inst) {
            DPRINTF(JY, "load [sn=%lli] has a preceding load [sn=%lli] which is predicted 'taken' by loop\n",
                    load_inst->seqNum, probe_inst->seqNum);
            // WARNING: must be put with prediction code
            // stall this load
            delete req;

            if (TheISA::HasUnalignedMemAcc && sreqLow) {
                delete sreqLow;
                delete sreqHigh;
            }

            load_inst->onlyWaitForExpose(true);
            iewStage->deferMemInst(load_inst);
            return NoFault;
        }
    }

    // Jiyong, MLDOM: prepare the packet
    if (sendSpecRead) {
        if (cpu->enableMLDOM) {
            // SDO style spec load request
            switch (load_inst->pred_level) {
              case Cache_L1: // predict L0
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to send a spec ld to L0 (pred_level = %d(L1))\n", load_inst->seqNum, load_idx, load_inst->pred_level);
                data_pkt = Packet::createReadSpecL0(req);
                break;
              case Cache_L2: // predict L1
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to send a spec ld to L1 (pred_level = %d(L2))\n", load_inst->seqNum, load_idx, load_inst->pred_level);
                data_pkt = Packet::createReadSpecL1(req);
                break;
              case Cache_L3: // predict L2
                if (cpu->delay_on_LLC_pred) {
                    // stall this spec request
                    delete req;
                    if (TheISA::HasUnalignedMemAcc && sreqLow) {
                        delete sreqLow;
                        delete sreqHigh;
                    }
                    load_inst->onlyWaitForExpose(true);
                    iewStage->deferMemInst(load_inst);
                    return NoFault;
                }
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to send a spec ld to L2 (pred_level = %d(L3))\n", load_inst->seqNum, load_idx, load_inst->pred_level);
                data_pkt = Packet::createReadSpecL2(req);
                break;
              case DRAM: // predict Mem
                if (cpu->delay_on_DRAM_pred) {
                    // stall this spec request
                    delete req;
                    if (TheISA::HasUnalignedMemAcc && sreqLow) {
                        delete sreqLow;
                        delete sreqHigh;
                    }
                    load_inst->onlyWaitForExpose(true);
                    iewStage->deferMemInst(load_inst);
                    return NoFault;
                }
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to send a spec ld to Mem (pred_level = %d(DRAM))\n", load_inst->seqNum, load_idx, load_inst->pred_level);
                data_pkt = Packet::createReadSpecMem(req);
                break;
              case Perfect_Level: // perfect safe prediction (modulo transient coherence state)
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to issue perfect oblS\n", load_inst->seqNum, load_idx);
                data_pkt = Packet::createReadSpecPerfect(req);
                break;
              case PerfectUnsafe_Level: // perfect unsafe prediction
                DPRINTF(JY, "ld_inst [sn:%lli], idx=%d wants to issue perfect unsafe oblS == normal ld\n", load_inst->seqNum, load_idx);
                data_pkt = Packet::createReadSpecPerfectUnsafe(req);
                break;
              default:
                assert(0 && "ERROR: predict unknown level\n");
            }
        }
        else {
            // SDO not enabled: Invisispec-style spec load request
            data_pkt = Packet::createReadSpec(req);
        }
    }
    else {
        // regular load request
        data_pkt = Packet::createRead(req);
    }

    data_pkt->dataStatic(load_inst->memData);

    LSQSenderState *state = new LSQSenderState;
    state->isLoad = true;
    state->idx = load_idx;
    state->inst = load_inst;
    data_pkt->senderState = state;
    data_pkt->seqNum = load_inst->seqNum;

    if (!TheISA::HasUnalignedMemAcc || !sreqLow) {
        // Point the first packet at the main data packet.
        fst_data_pkt = data_pkt;
        fst_data_pkt->setFirst();

        // Naive invisispec: use specbuffer
        if (sendSpecRead) {
            if (!cpu->enableMLDOM) {
                int src_idx = checkSpecBufHit(req, load_idx);
                if (src_idx != -1) {
                    data_pkt->setOnlyAccessSpecBuf();
                    data_pkt->srcIdx = src_idx;
                    specBufHits++;  // note blocked load will repeatly increment this counter
                } else {
                    specBufMisses++;  // note blocked load will repeatly increment this counter
                }
            }
        }

        // Jiyong, MLDOM: allow specbuffer hit for both regular ld and spec ld
        // This is INCORRECT since regular ld shouldn't use specld
        // only for testing purpose
        if (cpu->enableMLDOM && cpu->enableOblSContention) {
            // we allow both specld and regular ld to hit spec buffer
            int src_idx = checkSpecBufHit(req, load_idx);
            if (src_idx != -1) {
                data_pkt->setOnlyAccessSpecBuf();
                data_pkt->srcIdx = src_idx;
                specBufHits++;  // note blocked load will repeatly increment this counter
            } else {
                specBufMisses++;  // note blocked load will repeatly increment this counter
            }
        }

        fst_data_pkt->reqIdx = load_idx;
        fst_data_pkt->seqNum = load_inst->seqNum;
    } else {
        // Create the split packets.
        if(sendSpecRead){

            // Jiyong: create first and second packets based on enableMLDOM
            if (!cpu->enableMLDOM) {
                // Naive invisiSpec
                fst_data_pkt = Packet::createReadSpec(sreqLow);
                snd_data_pkt = Packet::createReadSpec(sreqHigh);

                int fst_src_idx = checkSpecBufHit(sreqLow, load_idx);
                if (fst_src_idx != -1) {
                    fst_data_pkt->setOnlyAccessSpecBuf();
                    fst_data_pkt->srcIdx = fst_src_idx;
                    specBufHits++;
                } else {
                    specBufMisses++;
                }

                int snd_src_idx = checkSpecBufHit(sreqHigh, load_idx);
                if (snd_src_idx != -1) {
                    snd_data_pkt->setOnlyAccessSpecBuf();
                    snd_data_pkt->srcIdx = snd_src_idx;
                    specBufHits++;
                } else {
                    specBufMisses++;
                }
            } else {
                // create dedicated packets
                switch (load_inst->pred_level) {
                    case Cache_L1: // predict L0
                        fst_data_pkt = Packet::createReadSpecL0(sreqLow);
                        snd_data_pkt = Packet::createReadSpecL0(sreqHigh);
                        break;
                    case Cache_L2: // predict L1
                        fst_data_pkt = Packet::createReadSpecL1(sreqLow);
                        snd_data_pkt = Packet::createReadSpecL1(sreqHigh);
                        break;
                    case Cache_L3: // predict L2
                        fst_data_pkt = Packet::createReadSpecL2(sreqLow);
                        snd_data_pkt = Packet::createReadSpecL2(sreqHigh);
                        break;
                    case DRAM: // predict Mem
                        fst_data_pkt = Packet::createReadSpecMem(sreqLow);
                        snd_data_pkt = Packet::createReadSpecMem(sreqHigh);
                        break;
                    case Perfect_Level: // perfect (safe) prediction
                        fst_data_pkt = Packet::createReadSpecPerfect(sreqLow);
                        snd_data_pkt = Packet::createReadSpecPerfect(sreqHigh);
                        break;
                    case PerfectUnsafe_Level:   // perfect (unsafe) prediction
                        fst_data_pkt = Packet::createReadSpecPerfectUnsafe(sreqLow);
                        snd_data_pkt = Packet::createReadSpecPerfectUnsafe(sreqHigh);
                        break;
                    default:
                        assert(0 && "ERROR: predict unknown level\n");
                }
            }
        } else {
            fst_data_pkt = Packet::createRead(sreqLow);
            snd_data_pkt = Packet::createRead(sreqHigh);
        }

        // Jiyong, MLDOM: allow specbuffer hit for both regular ld and spec ld
        // This is INCORRECT since regular ld shouldn't use specld
        // only for testing purpose
        if (cpu->enableMLDOM && cpu->enableOblSContention) {
            // we allow both regular load and spec ld to hit spec buffer
            int fst_src_idx = checkSpecBufHit(sreqLow, load_idx);
            if (fst_src_idx != -1) {
                fst_data_pkt->setOnlyAccessSpecBuf();
                fst_data_pkt->srcIdx = fst_src_idx;
                specBufHits++;
            } else {
                specBufMisses++;
            }

            int snd_src_idx = checkSpecBufHit(sreqHigh, load_idx);
            if (snd_src_idx != -1) {
                snd_data_pkt->setOnlyAccessSpecBuf();
                snd_data_pkt->srcIdx = snd_src_idx;
                specBufHits++;
            } else {
                specBufMisses++;
            }
        }

        fst_data_pkt->setFirst();
        fst_data_pkt->dataStatic(load_inst->memData);
        snd_data_pkt->dataStatic(load_inst->memData + sreqLow->getSize());

        fst_data_pkt->senderState = state;
        snd_data_pkt->senderState = state;
        fst_data_pkt->reqIdx = load_idx;
        snd_data_pkt->reqIdx = load_idx;
        fst_data_pkt->seqNum = load_inst->seqNum;
        snd_data_pkt->seqNum = load_inst->seqNum;

        fst_data_pkt->isSplit = true;
        snd_data_pkt->isSplit = true;
        state->isSplit = true;
        state->outstanding = 2;
        state->mainPkt = data_pkt;
    }


    //[> solution 2: send the load and force it to alias with the probe_load <]
    //// If the load is predicted by the loop and "not taken", it can be stalled by a pending "taken"
    //if (sendSpecRead && load_inst->chosen_pred_type == Loop && load_inst->pred_level == Cache_L1) {
        //DPRINTF(JY, "load [sn=%lli] is predicted as L1 by loop\n", load_inst->seqNum);
        //DynInstPtr probe_inst = findProbeLoad(load_inst);

        //if (probe_inst) {
            //DPRINTF(JY, "load [sn=%lli] has a preceding load [sn=%lli] which is predicted 'taken' by loop\n",
                    //load_inst->seqNum, probe_inst->seqNum);
            //fst_data_pkt->aliased_reqIdx = probe_inst->lqIdx;
            //DPRINTF(JY, "load [sn=%lli]'s 1st packet has bond with probe load [sn=%lli, lq_idx=%d]\n",
                    //load_inst->seqNum, probe_inst->seqNum, probe_inst->lqIdx);
            //if (TheISA::HasUnalignedMemAcc && sreqLow) {
                //snd_data_pkt->aliased_reqIdx = probe_inst->lqIdx;
                //DPRINTF(JY, "load [sn=%lli]'s 2nd packet has bond with probe load [sn=%lli, lq_idx=%d]\n",
                        //load_inst->seqNum, probe_inst->seqNum, probe_inst->lqIdx);
            //}
        //}
    //}

        //// solution 3: let memory controller make the decision
        //fst_data_pkt->notTakenByLoop = true;
        //if (TheISA::HasUnalignedMemAcc && sreqLow)
            //snd_data_pkt->notTakenByLoop = true;
    //}
    //else if (sendSpecRead && load_inst->chosen_pred_type == Loop && load_inst->pred_level != Cache_L1) {
        //fst_data_pkt->takenByLoop = true;
        //if (TheISA::HasUnalignedMemAcc && sreqLow)
            //snd_data_pkt->takenByLoop = true;
    //}

    // For now, load throughput is constrained by the number of
    // load FUs only, and loads do not consume a cache port (only
    // stores do).
    // @todo We should account for cache port contention
    // and arbitrate between loads and stores.
    bool successful_load = true;
    // MARK: here is the place memory request of read is sent [mengjia]
    // [SafeSpec] Sending out a memory request
    if (!dcachePort->sendTimingReq(fst_data_pkt)) {
        successful_load = false;
    } else if (TheISA::HasUnalignedMemAcc && sreqLow) {
        completedFirst = true;

        // The first packet was sent without problems, so send this one
        // too. If there is a problem with this packet then the whole
        // load will be squashed, so indicate this to the state object.
        // The first packet will return in completeDataAccess and be
        // handled there.
        // @todo We should also account for cache port contention
        // here.
        if (!dcachePort->sendTimingReq(snd_data_pkt)) {
            // The main packet will be deleted in completeDataAccess.
            state->complete();
            // Signify to 1st half that the 2nd half was blocked via state
            state->cacheBlocked = true;
            successful_load = false;
        }
    }

    // If the cache was blocked, or has become blocked due to the access,
    // handle it.
    if (!successful_load) {
        if (!sreqLow) {
            // Packet wasn't split, just delete main packet info
            delete state;
            delete req;
            delete data_pkt;
        }

        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            if (!completedFirst) {
                // Split packet, but first failed.  Delete all state.
                delete state;
                delete req;
                delete data_pkt;
                delete fst_data_pkt;
                delete snd_data_pkt;
                delete sreqLow;
                delete sreqHigh;
                sreqLow = NULL;
                sreqHigh = NULL;
            } else {
                // Can't delete main packet data or state because first packet
                // was sent to the memory system
                delete data_pkt;
                delete req;
                delete sreqHigh;
                delete snd_data_pkt;
                sreqHigh = NULL;
            }
        }

        ++lsqCacheBlocked;

        iewStage->blockMemInst(load_inst);

        // No fault occurred, even though the interface is blocked.
        return NoFault;
    }

    auto it = delay_count.find(load_inst->pcState().instAddr());
    if (it == delay_count.end())
        delay_count[load_inst->pcState().instAddr()] = load_inst->delayedCycleCnt;
    else
        delay_count[load_inst->pcState().instAddr()] += load_inst->delayedCycleCnt;

    //int64_t num_cycle = cpu->numCycles.value();
    //if (num_cycle % 1000000 == 0) {
        //printf("Cycle: %ld\n", num_cycle);
        //for (auto it2 = delay_count.begin(); it2 != delay_count.end(); it2++)
            //printf("addr: %lx; delayed_cycles: %lu\n", it2->first, it2->second);
        //printf("\n");
    //}
    DPRINTF(JY, "A load [sn:%lli] is executed(access cache) at cycle %lld. PC: %s, 0x%x; instr: %s; delayed: %d\n",
            load_inst->seqNum, cpu->numCycles.value(), load_inst->pcState(), load_inst->pcState().instAddr(), load_inst->staticInst->disassemble(load_inst->instAddr()), load_inst->delayedCycleCnt);
    DPRINTF(LSQUnit, "successfully sent out packet(s) for inst [sn:%lli]\n",
            load_inst->seqNum);

    load_inst->issueCycle = cpu->numCycles.value();
    load_inst->loadLatency = cpu->numCycles.value();

    // Jiyong, MLDOM: check if any inflight oblS (tainted/untainted, commplete/incomplete) is aliased with
    // this oblS to issue. If so, we can save cycles by potentially reuse the result of previous oblS
    // TODO: make a specbuffer
    if (cpu->enableMLDOM) {
        int lq_idx = loadHead;
        while (lq_idx != load_idx) {
            DynInstPtr comp_load_inst = loadQueue[lq_idx];
            Addr line_addr = load_inst->effAddr & cacheBlockMask;
            Addr comp_line_addr = comp_load_inst->effAddr & cacheBlockMask;

            if (line_addr == comp_line_addr) {
                if (comp_load_inst->oblS_Sent && !comp_load_inst->readyToExpose()) {
                    if (comp_load_inst->oblS_Complete && comp_load_inst->oblS_Hit)
                        // prior oblS received all packets and hit
                        load_inst->alias_tainted_arrived = true;
                    else
                        // prior oblS isn't finished yet
                        load_inst->alias_tainted_not_arrived = true;
                }
                else if (loadQueue[lq_idx]->oblS_Sent && loadQueue[lq_idx]->readyToExpose()) {
                    if (comp_load_inst->oblS_Complete && comp_load_inst->oblS_Hit)
                        // prior oblS received all packets and hit
                        load_inst->alias_untainted_arrived = true;
                    else
                        // prior oblS isn't finished yet
                        load_inst->alias_untainted_not_arrived = true;
                }
            }
            // incrLdIdx(lq_idx)
            if ((++lq_idx) >= LQEntries)
                lq_idx = 0;
        }
    }

    // Jiyong, SDO: print out sent packet information
    if (sendSpecRead && cpu->enableMLDOM)
        DPRINTF(JY_SDO_Pred, "<Send> A load PC:%s, [sn:%lli] just issued a OblS, pred_level = %d\n",
                load_inst->pcState(), load_inst->seqNum, load_inst->pred_level);
    else
        DPRINTF(JY_SDO_Pred, "<Send> A load PC:%s, [sn:%lli] just issued a normal load\n",
                load_inst->pcState(), load_inst->seqNum);

    // Based on the state when the load is issued, set the flags (especially needExposeOnly)
    if (sendSpecRead && cpu->enableMLDOM) {  // Jiyong: MLDOM mode
        // Jiyong:  we don't know whether we should expose/validate until the load returns
        //          but we use the original condition for expose as a hint
        //          if the load is a hit, we will do expose; otherwise we do validation
        load_inst->oblS_Sent = true;
        load_inst->MLDOM_state = MLDOM_STATE_A;
        if (TheISA::HasUnalignedMemAcc && sreqLow)
            DPRINTF(JY, "A load [sn:%lli] just issued a SpecLD (2 packets), now in state A\n", load_inst->seqNum);
        else
            DPRINTF(JY, "A load [sn:%lli] just issued a SpecLD (1 packet), now in state A\n", load_inst->seqNum);

        if (load_inst->pred_level == Cache_L1)
            numSpecLdL0Sent++;
        else if (load_inst->pred_level == Cache_L2)
            numSpecLdL1Sent++;
        else if (load_inst->pred_level == Cache_L3)
            numSpecLdL2Sent++;
        else if (load_inst->pred_level == DRAM)
            numSpecLdMemSent++;
        else if (load_inst->pred_level == Perfect_Level)
            numSpecLdPerfectSent++;
        else if (load_inst->pred_level == PerfectUnsafe_Level)
            numSpecLdPerfectUnsafeSent++;
        else
            assert(0);

        if (cpu->needsTSO && !load_inst->isDataPrefetch()) {
            if ( checkPrevLoadsExecuted(load_idx) ) {
                load_inst->needExposeOnly(true);
                DPRINTF(JY, "Set load PC %s, [sn:%lli] as needExposeOnly (if it's a hit)\n", load_inst->pcState(), load_inst->seqNum);
            } else {
                DPRINTF(JY, "Set load PC %s, [sn:%lli] as needValidation\n", load_inst->pcState(), load_inst->seqNum);
            }
        }
        else {
            load_inst->needExposeOnly(true);
            DPRINTF(JY, "Set load PC %s, [sn:%lli] as needExposeOnly (if it's a hit)\n", load_inst->pcState(), load_inst->seqNum);
        }
        load_inst->needPostFetch(true);
        assert(!req->isMmappedIpr());

        // create request for the second load
        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            load_inst->postReq = new Request(*req);
            load_inst->postSreqLow = new Request(*sreqLow);
            load_inst->postSreqHigh = new Request(*sreqHigh);
        }
        else {
            load_inst->postReq = new Request(*req);
            load_inst->postSreqLow = NULL;
            load_inst->postSreqHigh = NULL;
        }
        load_inst->needDeletePostReq(true);
        DPRINTF(LSQUnit, "created validation/expose request for inst [sn:%lli] req=%#x, reqLow=%#x, reqHigh=%#x\n",
                    load_inst->seqNum,
                    (Addr)(load_inst->postReq),
                    (Addr)(load_inst->postSreqLow),
                    (Addr)(load_inst->postSreqHigh));
    }
    else if (sendSpecRead && !cpu->enableMLDOM) { // Naive InvisiSpec
        assert(0);
        // [mengjia] Here we set the needExposeOnly flag
        if (cpu->needsTSO && !load_inst->isDataPrefetch()){
            // need to check whether previous load_instructions specComplete or not
            if ( checkPrevLoadsExecuted(load_idx) ){
                load_inst->needExposeOnly(true);
                DPRINTF(LSQUnit, "Set load PC %s, [sn:%lli] as needExposeOnly\n", load_inst->pcState(), load_inst->seqNum);
            } else {
                DPRINTF(LSQUnit, "Set load PC %s, [sn:%lli] as needValidation\n", load_inst->pcState(), load_inst->seqNum);
            }
        } else {
            //if RC, always only need expose
            load_inst->needExposeOnly(true);
            DPRINTF(LSQUnit, "Set load PC %s, [sn:%lli] as needExposeOnly\n", load_inst->pcState(), load_inst->seqNum);
        }

        load_inst->needPostFetch(true);
        assert(!req->isMmappedIpr());
        //save expose requestPtr
        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            load_inst->postSreqLow = new Request(*sreqLow);
            load_inst->postSreqHigh = new Request(*sreqHigh);
            load_inst->postReq = NULL;
        } else {
            load_inst->postReq = new Request(*req);
            load_inst->postSreqLow = NULL;
            load_inst->postSreqHigh = NULL;
        }
        load_inst->needDeletePostReq(true);
        DPRINTF(LSQUnit, "created validation/expose request for inst [sn:%lli] req=%#x, reqLow=%#x, reqHigh=%#x\n",
                    load_inst->seqNum,
                    (Addr)(load_inst->postReq),
                    (Addr)(load_inst->postSreqLow),
                    (Addr)(load_inst->postSreqHigh));
    } else {    // Normal load
        numNonSpecLdSent++;
        load_inst->setExposeCompleted();
        load_inst->needPostFetch(false);
        if (TheISA::HasUnalignedMemAcc && sreqLow) {
            setSpecBufState(sreqLow);
            setSpecBufState(sreqHigh);
        } else {
            setSpecBufState(req);
        }
    }

    return NoFault;
}

template <class Impl>
Fault
LSQUnit<Impl>::write(Request *req, Request *sreqLow, Request *sreqHigh,
                     uint8_t *data, int store_idx)
{
    assert(storeQueue[store_idx].inst);

    DPRINTF(LSQUnit, "Doing write to store idx %i, addr %#x"
            " | storeHead:%i [sn:%i]\n",
            store_idx, req->getPaddr(), storeHead,
            storeQueue[store_idx].inst->seqNum);

    storeQueue[store_idx].req = req;
    storeQueue[store_idx].sreqLow = sreqLow;
    storeQueue[store_idx].sreqHigh = sreqHigh;
    unsigned size = req->getSize();
    storeQueue[store_idx].size = size;
    bool store_no_data = req->getFlags() & Request::STORE_NO_DATA;
    storeQueue[store_idx].isAllZeros = store_no_data;
    assert(size <= sizeof(storeQueue[store_idx].data) || store_no_data);

    // Split stores can only occur in ISAs with unaligned memory accesses.  If
    // a store request has been split, sreqLow and sreqHigh will be non-null.
    if (TheISA::HasUnalignedMemAcc && sreqLow) {
        storeQueue[store_idx].isSplit = true;
    }

    if (!(req->getFlags() & Request::CACHE_BLOCK_ZERO) && \
        !req->isCacheMaintenance())
        memcpy(storeQueue[store_idx].data, data, size);

    // This function only writes the data to the store queue, so no fault
    // can happen here.
    return NoFault;
}



#endif // __CPU_O3_LSQ_UNIT_HH__
