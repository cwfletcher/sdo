/*
 * Location predictors for Speculative Data Oblivious Execution
 */

#ifndef __CPU_O3_LOC_PRED_HH__
#define __CPU_O3_LOC_PRED_HH__

#include <array>
#include <deque>
#include <vector>
#include <iostream>
#include <utility>
#include <list>
#include <string>
#include <map>

#include "arch/isa_traits.hh"
#include "base/types.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"
#include "cpu/timebuf.hh"

class LocPred_BaseType;

typedef enum {
    Static = 0,
    Greedy,
    Hysteresis,
    Local,
    Loop,
    Tournament_2Way,
    Tournament_3Way,
    Random,
    Perfect,
    None
} LocPred_t;

typedef enum {
    Cache_L1 = 0,       // L0 in MESI_Three_Level
    Cache_L2,           // L1 in MESI_Three_Level
    Cache_L3,           // L2 in MESI_Three_Level
    DRAM,               // Memory
    Cache_L1c,          // Same as Cache_L1, used by hysteresis
    Perfect_Level,      // Target level for safe perfect predictor
    PerfectUnsafe_Level,// Target level for unsafe perfect predictor
} CacheLevel_t;

LocPred_t string_to_pred_type(const std::string pred_str);

LocPred_BaseType* pred_type_to_pred(LocPred_t pred_type);

class LocPred_BaseType {
  public:

    LocPred_BaseType();
    LocPred_BaseType(uint64_t pred_size);
    LocPred_BaseType(LocPred_t pred_type);
    LocPred_BaseType(LocPred_t pred_type, uint64_t pred_size);
    virtual ~LocPred_BaseType() {}

    // Prepare the location predictor when the load is inserted into ROB/LQ
    // @param: load_inst: the new dynamic (load)
    virtual void prepare(Addr load_PC, InstSeqNum load_sn) = 0;

    // Looks up the dynamic instr in the cache level predictor and make the prediction
    // @param  load_inst: dynamic (load) instruction
    // @return the predicted cache level
    virtual CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred) = 0;

    // Updates the cache level predictor with the actual result of a load
    // @param: load_inst: dynamic (load) instruction
    // @param: actual_level: actual cache level in which load got hit
    // @return: if another update at instruction commit is required
    virtual int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level) = 0;

    // Called when the load is committed
    virtual void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit) = 0;

    // Squash the predictor
    // @param: squashed_inst: the instruction causing squash
    virtual void squash(InstSeqNum squashed_sn) = 0;

    // print part of the predictor table
    virtual void print(Addr load_PC) = 0;

    inline unsigned getLocalIndex(Addr load_addr);
    inline LocPred_t getPredType();

  protected:
    uint64_t predictorSize;
    unsigned indexMask;
    LocPred_t pred_type;
};

class LocPred_random : public LocPred_BaseType {
  public:
    LocPred_random() : LocPred_BaseType(Random) {}

    ~LocPred_random() {}

    void prepare(Addr load_PC, InstSeqNum load_sn) {}

    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred) {
        chosen_pred = Random;
        switch (rand() % 4) {
          case 0:
            return Cache_L1;
          case 1:
            return Cache_L2;
          case 2:
            return Cache_L3;
          case 3:
            return DRAM;
          default:
            return Cache_L2;
        }
    }
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level) {
        return 0;
    }

    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit) {}

    void squash(InstSeqNum squashed_sn) {}

    void print(Addr load_PC) {}

};

class LocPred_perfect : public LocPred_BaseType {
  public:
    LocPred_perfect() : LocPred_BaseType(Perfect), is_safe(false) {}
    LocPred_perfect(bool perfect_is_safe) : LocPred_BaseType(Perfect), is_safe(perfect_is_safe) {}

    ~LocPred_perfect() {}

    void prepare(Addr load_PC, InstSeqNum load_sn) {}

    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred) {
        chosen_pred = Perfect;
        if (is_safe)
            return Perfect_Level;
        else
            return PerfectUnsafe_Level;
    }

    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level) {
        return 0;
    }

    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit) {}

    void squash(InstSeqNum squashed_sn) {}

    void print(Addr load_PC) {}

    bool is_perfect_safe() { return is_safe; }

  private:
    bool is_safe;
};

class LocPred_static : public LocPred_BaseType {
  public:
    LocPred_static() : LocPred_BaseType(Static), prediction(Cache_L2) {}  // default is L2 for best perf

    LocPred_static(CacheLevel_t pred_level) : LocPred_BaseType(Static), prediction(pred_level) {}

    ~LocPred_static() {}

    void prepare(Addr load_PC, InstSeqNum load_sn) {}

    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred) {
        chosen_pred = Static;
        return prediction;
    }

    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level) {
        return 0;
    }

    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit) {}

    void squash(InstSeqNum squashed_sn) {}

    void print(Addr load_PC) {}

  private:
    CacheLevel_t prediction;
};

// stores last N levels seen and predicts the lower level of the 2. eg : if last 2 were L3 and L2, it will predict L3
class LocPred_greedy : public LocPred_BaseType {
  public:
    LocPred_greedy();
    LocPred_greedy(uint64_t pred_size);
    LocPred_greedy(uint64_t pred_size, unsigned history_len);
    ~LocPred_greedy() {}

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    // Length of history
    unsigned historyLength;

    // Array of last historyLength cache levels seen by the load
    std::vector<std::vector<CacheLevel_t> > history;
};

// stores last N levels seen and changes its current prediction only if it sees 2 consecutive same levels (2 bit br predictor)
class LocPred_hysteresis : public LocPred_BaseType {
  public:
    LocPred_hysteresis();
    LocPred_hysteresis(uint64_t pred_size);
    ~LocPred_hysteresis() {}

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    //// Array of last historyLength cache levels seen by the load
    std::vector<CacheLevel_t> lastLevel1; // last seen
    std::vector<CacheLevel_t> lastLevel2; // 2nd last seen

};

class LocPred_local : public LocPred_BaseType {
  public:
    LocPred_local();
    LocPred_local(uint64_t pred_size);
    LocPred_local(uint64_t pred_size, unsigned history_len);
    ~LocPred_local() {}

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    unsigned historyLength;

    // Array of last historyLength cache levels seen by the load
    std::vector<std::deque<CacheLevel_t> > history;
    std::vector<std::deque<InstSeqNum> >   history_sn;

    std::vector<std::vector<CacheLevel_t> > prediction_table;

};

class LocPred_loop : public LocPred_BaseType {
  public:
    LocPred_loop();
    LocPred_loop(uint64_t pred_size);
    ~LocPred_loop() {}

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    std::vector<std::deque<bool> >       direction_history;
    std::vector<std::deque<InstSeqNum> > direction_history_sn;
    std::vector<int>                     loop_count_table;
    std::vector<int>                     new_loop_count_table;
    std::vector<CacheLevel_t>            pred_level_table;
    std::vector<CacheLevel_t>            new_pred_level_table;
};

class LocPred_tournament_2Way : public LocPred_BaseType {
  public:
    LocPred_tournament_2Way();
    LocPred_tournament_2Way(uint64_t pred_size);
    LocPred_tournament_2Way(uint64_t pred_size, int conf_bit);
    LocPred_tournament_2Way(LocPred_t pred1_type, LocPred_t pred2_type,
                            uint64_t pred_size);
    LocPred_tournament_2Way(LocPred_t pred1_type, LocPred_t pred2_type,
                            uint64_t pred_size, int conf_bit);
    ~LocPred_tournament_2Way();

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    std::vector<int> pred1_conf;
    std::vector<int> pred2_conf;

    LocPred_BaseType* pred1;
    LocPred_BaseType* pred2;

    int conf_max;
};

class LocPred_tournament_3Way : public LocPred_BaseType {
  public:
    LocPred_tournament_3Way();
    LocPred_tournament_3Way(uint64_t pred_size);
    LocPred_tournament_3Way(uint64_t pred_size, int conf_bit);
    LocPred_tournament_3Way(LocPred_t pred1_type, LocPred_t pred2_type, LocPred_t pred3_type,
                            uint64_t pred_size);
    LocPred_tournament_3Way(LocPred_t pred1_type, LocPred_t pred2_type, LocPred_t pred3_type,
                            uint64_t pred_size, int conf_bit);
    ~LocPred_tournament_3Way();

    void prepare(Addr load_PC, InstSeqNum load_sn);
    CacheLevel_t predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred);
    int update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level);
    void commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit);
    void squash(InstSeqNum squashed_sn);
    void print(Addr load_PC);

  private:
    LocPred_BaseType* pred1;
    LocPred_BaseType* pred2;
    LocPred_BaseType* pred3;

    std::vector<int> pred1_conf;
    std::vector<int> pred2_conf;
    std::vector<int> pred3_conf;

    int conf_max;
};



#endif

