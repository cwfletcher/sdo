/*
 * Location predictors for Speculative Data Oblivious Execution
 */

#ifndef __CPU_O3_LOC_PRED_IMPL_HH__
#define __CPU_O3_LOC_PRED_IMPL_HH__

#include "cpu/o3/locPred.hh"

#include "base/types.hh"
#include "arch/generic/debugfaults.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"
#include "debug/LocPred.hh"

LocPred_t string_to_pred_type(const std::string pred_str) {

    if (pred_str.compare("static") == 0)
        return Static;
    else if (pred_str.compare("greedy") == 0)
        return Greedy;
    else if (pred_str.compare("hysteresis") == 0)
        return Hysteresis;
    else if (pred_str.compare("local") == 0)
        return Local;
    else if (pred_str.compare("loop") == 0)
        return Loop;
    else if (pred_str.compare("tournament_2way") == 0)
        return Tournament_2Way;
    else if (pred_str.compare("tournament_3way") == 0)
        return Tournament_3Way;
    else if (pred_str.compare("random") == 0)
        return Random;
    else if (pred_str.compare("perfect") == 0)
        return Perfect;
    else
        return None;
}

LocPred_BaseType* pred_type_to_pred(LocPred_t pred_type) {
    LocPred_BaseType* pred;
    switch (pred_type) {
      case Static:
        pred = new LocPred_static();
        break;
      case Greedy:
        pred = new LocPred_greedy();
        break;
      case Hysteresis:
        pred = new LocPred_hysteresis();
        break;
      case Local:
        pred = new LocPred_local();
        break;
      case Loop:
        pred = new LocPred_loop();
        break;
      case Tournament_2Way:
        pred = new LocPred_tournament_2Way();
        break;
      case Tournament_3Way:
        pred = new LocPred_tournament_3Way();
        break;
      case Random:
        pred = new LocPred_random();
        break;
      case Perfect:
        pred = new LocPred_perfect();
        break;
      default:
        printf("ERROR: unknwon pred_type %d\n", pred_type);
        assert(0);
        pred = new LocPred_static();
    }
    return pred;
}

// Namrata, default predictor size
unsigned default_pred_size = 8192;
unsigned default_history_length = 4;
unsigned default_conf_length = 4;


/******** Namrate, Jiyong MLDOM: define location predictors *************/
LocPred_BaseType::LocPred_BaseType()
    : predictorSize(default_pred_size),
      indexMask(default_pred_size - 1)
{
}

LocPred_BaseType::LocPred_BaseType(uint64_t pred_size)
    : predictorSize(pred_size),
      indexMask(pred_size - 1)
{
}

LocPred_BaseType::LocPred_BaseType(LocPred_t pred_type)
    : predictorSize(default_pred_size),
      indexMask(default_pred_size - 1),
      pred_type(pred_type)
{
}

LocPred_BaseType::LocPred_BaseType(LocPred_t pred_type, uint64_t pred_size)
    : predictorSize(pred_size),
      indexMask(pred_size - 1),
      pred_type(pred_type)
{
}

unsigned
LocPred_BaseType::getLocalIndex(Addr load_addr)
{
    return load_addr & this->indexMask;
}

LocPred_t
LocPred_BaseType::getPredType()
{
    return pred_type;
}


/********** Greedy **************/
LocPred_greedy::LocPred_greedy()
    : LocPred_BaseType(Greedy),
      historyLength(default_history_length),  // history length has default value 2
      history(default_pred_size, std::vector<CacheLevel_t>(historyLength, DRAM))
{
}

LocPred_greedy::LocPred_greedy(uint64_t pred_size)
    : LocPred_BaseType(Greedy, pred_size),
      historyLength(default_history_length),
      history(pred_size, std::vector<CacheLevel_t>(historyLength, DRAM))
{
}

LocPred_greedy::LocPred_greedy(uint64_t pred_size, unsigned history_len)
    : LocPred_BaseType(Greedy, pred_size),
      historyLength(history_len),
      history(pred_size, std::vector<CacheLevel_t>(historyLength, DRAM))
{
}

void
LocPred_greedy::prepare(Addr load_PC, InstSeqNum load_sn)
{
    DPRINTF(LocPred, "greedy: Prepare(%lu, %ld)\n", load_PC, load_sn);
}

CacheLevel_t
LocPred_greedy::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    chosen_pred = Greedy;

    DPRINTF(LocPred, "greedy: Predict(%lu, %ld)\n", load_PC, load_sn);

    // Greedy predictor predicts the lowest level in the history
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    //// find the corresponding history entry
    //unsigned history_idx = 0;
    //bool     history_idx_valid = false;
    //for (; history_idx < history[pred_entry_idx].size(); history_idx++) {
        //if (history_sn[pred_entry_idx][history_idx] == load_sn) {
            //history_idx_valid = true;
            //break;
        //}
    //}

    //assert (history_idx + historyLength < history[pred_entry_idx].size());
    //assert (history_idx_valid);

    //CacheLevel_t pred = *std::max_element(history[pred_entry_idx].begin() + history_idx + 1,
                                          //history[pred_entry_idx].begin() + history_idx + 1 + historyLength);

    //history[pred_entry_idx][history_idx] = pred;
    CacheLevel_t pred = *std::max_element(history[pred_entry_idx].begin(), history[pred_entry_idx].end());

    return pred;
}

int
LocPred_greedy::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    DPRINTF(LocPred, "greedy: Update(%lu, %ld, %d)\n", load_PC, load_sn, actual_level);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    //// find the corresponding history entry
    //unsigned history_idx = 0;
    //bool     history_idx_valid = false;
    //for (; history_idx < history[pred_entry_idx].size(); history_idx++) {
        //if (history_sn[pred_entry_idx][history_idx] == load_sn) {
            //history_idx_valid = true;
            //break;
        //}
    //}

    //if (!history_idx_valid)
        //return 0;

    //history[pred_entry_idx][history_idx] = actual_level;
    int i = 0;
    for (; i < history[pred_entry_idx].size()-1; i++)
        history[pred_entry_idx][i] = history[pred_entry_idx][i+1];

    history[pred_entry_idx][i] = actual_level;

    return 0;
}

void
LocPred_greedy::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
    DPRINTF(LocPred, "greedy: Commit(%lu, %ld, %d)\n", load_PC, load_sn, update_on_commit);
}

void
LocPred_greedy::squash(InstSeqNum squashed_sn)
{
    DPRINTF(LocPred, "greedy: squash(%ld)\n", squashed_sn);
}

void
LocPred_greedy::print(Addr load_PC)
{
    //unsigned pred_entry_idx = getLocalIndex(load_PC);
    //printf("Pred data for 0x%lx\n", load_PC);
    //printf("\n==  <Greedy> History Table <idx=%u> ==\n", pred_entry_idx);

    //printf("size = %lu | %lu\n", history[pred_entry_idx].size(),
            //history_sn[pred_entry_idx].size());

    //printf("%p, %p\n", (void*)&(history_sn[pred_entry_idx].front()),
                       //(void*)&(history_sn[pred_entry_idx].back()));

    //printf("%p, %p\n", (void*)&(history[pred_entry_idx].front()),
                       //(void*)&(history[pred_entry_idx].back()));

    //printf("#: level | seqNum\n");

    //for (int i = 0; i < history[pred_entry_idx].size(); i++)
        //printf ("%d: %d | %ld\n", i, history[pred_entry_idx][i], history_sn[pred_entry_idx][i]);
}

/************ Hysteresis **********/
LocPred_hysteresis::LocPred_hysteresis()
    : LocPred_BaseType(Hysteresis),
      lastLevel1(default_pred_size, DRAM),
      lastLevel2(default_pred_size, DRAM)
{
}

LocPred_hysteresis::LocPred_hysteresis(uint64_t pred_size)
    : LocPred_BaseType(Hysteresis, pred_size),
      lastLevel1(pred_size, DRAM),
      lastLevel2(pred_size, DRAM)
{
}

void
LocPred_hysteresis::prepare(Addr load_PC, InstSeqNum load_sn)
{
}

CacheLevel_t
LocPred_hysteresis::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    chosen_pred = Hysteresis;

    // Hysteresis predictor predicts L1 unless no L1 is in the history
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    CacheLevel_t level1 = lastLevel1[pred_entry_idx];
    CacheLevel_t level2 = lastLevel2[pred_entry_idx];

    CacheLevel_t pred;
    if (level1 == Cache_L1c || level2 == Cache_L1c)
        pred = Cache_L1;
    else
        pred = level1 > level2 ? level1 : level2;

    return pred;
}

int
LocPred_hysteresis::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    // Update the local predictor.
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    CacheLevel_t ll1 = lastLevel1[pred_entry_idx];
    CacheLevel_t ll2 = lastLevel2[pred_entry_idx];

    if (actual_level != Cache_L1 || (ll2 != Cache_L1c && ll1 != Cache_L1) ){
        lastLevel2[pred_entry_idx] = ll1;
        lastLevel1[pred_entry_idx] = actual_level;
    }
    else
        lastLevel1[pred_entry_idx] = Cache_L1c;

    return 0;
}

void
LocPred_hysteresis::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
}

void
LocPred_hysteresis::squash(InstSeqNum squashed_sn)
{
}

void
LocPred_hysteresis::print(Addr load_PC)
{
}

/******* local ************/
LocPred_local::LocPred_local()
    : LocPred_BaseType(Local),
      historyLength(default_history_length),
      history(default_pred_size, std::deque<CacheLevel_t>(historyLength, DRAM)),
      history_sn(default_pred_size, std::deque<InstSeqNum>(historyLength, 0)),
      prediction_table(default_pred_size, std::vector<CacheLevel_t>(1 << (2*historyLength), DRAM))
{
}

LocPred_local::LocPred_local(uint64_t pred_size)
    : LocPred_BaseType(Local, pred_size),
      historyLength(default_history_length),
      history(pred_size, std::deque<CacheLevel_t>(historyLength, DRAM)),
      history_sn(pred_size, std::deque<InstSeqNum>(historyLength, 0)),
      prediction_table(pred_size, std::vector<CacheLevel_t>(1 << (2*historyLength), DRAM))
{
}

LocPred_local::LocPred_local(uint64_t pred_size, unsigned history_len)
    : LocPred_BaseType(Local, pred_size),
      historyLength(history_len),
      history(pred_size, std::deque<CacheLevel_t>(historyLength, DRAM)),
      history_sn(pred_size, std::deque<InstSeqNum>(historyLength, 0)),
      prediction_table(pred_size, std::vector<CacheLevel_t>(1 << (2*historyLength), DRAM))
{
}

void
LocPred_local::prepare(Addr load_PC, InstSeqNum load_sn)
{
    DPRINTF(LocPred, "local: prepare(%lu, %ld)\n", load_PC, load_sn);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    history[pred_entry_idx].push_front(Cache_L1);
    history_sn[pred_entry_idx].push_front(load_sn);
}

CacheLevel_t
LocPred_local::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    chosen_pred = Local;

    DPRINTF(LocPred, "local: predict(%lu, %ld)\n", load_PC, load_sn);

    // local predictor checks PHT entry, then BHT entry. True -> DRAM; else -> L0
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    // find the corresponding prediction_tables entry
    unsigned history_idx = 0;
    bool     M5_VAR_USED history_idx_valid = false;
    for (; history_idx < history[pred_entry_idx].size(); history_idx++) {
        if (history_sn[pred_entry_idx][history_idx] == load_sn) {
            history_idx_valid = true;
            break;
        }
    }
    assert (history_idx_valid);

    unsigned prediction_table_idx = 0;
    for (int i = history_idx + 1; i < history_idx + 1 + historyLength; i++) {
        prediction_table_idx = prediction_table_idx << 2;
        prediction_table_idx += history[pred_entry_idx][i];
    }

    // find the prediction in prediction_table
    CacheLevel_t pred = prediction_table[pred_entry_idx][prediction_table_idx];

    history[pred_entry_idx][history_idx] = pred;

    return pred;
}

int
LocPred_local::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    DPRINTF(LocPred, "local: update(%lu, %ld, %d)\n", load_PC, load_sn, actual_level);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    // find the corresponding entry
    unsigned history_idx = 0;
    bool     history_idx_valid = false;
    for (; history_idx < history[pred_entry_idx].size(); history_idx++) {
        if (history_sn[pred_entry_idx][history_idx] == load_sn) {
            history_idx_valid = true;
            break;
        }
    }

    if (!history_idx_valid)
        return 0;

    unsigned prediction_table_idx = 0;
    for (int i = history_idx + 1; i < history_idx + 1 + historyLength; i++) {
        prediction_table_idx = prediction_table_idx << 2;
        prediction_table_idx += history[pred_entry_idx][i];
    }

    // update history
    history[pred_entry_idx][history_idx] = actual_level;

    // update prediction_table
    prediction_table[pred_entry_idx][prediction_table_idx] = actual_level;

    return 1;
}

void
LocPred_local::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
    DPRINTF(LocPred, "local: commit(%lu, %ld, %d)\n", load_PC, load_sn, update_on_commit);

    if (update_on_commit) {
        unsigned pred_entry_idx = getLocalIndex(load_PC);

        history[pred_entry_idx].pop_back();
        history_sn[pred_entry_idx].pop_back();
    }
}

void
LocPred_local::squash(InstSeqNum squashed_sn)
{
    DPRINTF(LocPred, "local: squash(%ld)\n", squashed_sn);

    for (int i = 0; i < predictorSize; i++) {
        while (!history[i].empty()) {
            if (history_sn[i].front() > squashed_sn) {
                history[i].pop_front();
                history_sn[i].pop_front();
            }
            else {
                break;
            }
        }
    }
}

void
LocPred_local::print(Addr load_PC)
{
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    printf("Pred data for 0x%lx\n", load_PC);

    printf("\n== <Local> History Table  ==\n");
    printf("#: level | seqNum\n");
    for (int i = 0; i < history[pred_entry_idx].size(); i++)
        printf ("%d: %d | %ld\n", i, history[pred_entry_idx][i], history_sn[pred_entry_idx][i]);
    printf("size = %lu\n", history[pred_entry_idx].size());

    printf("\n== <Local> Prediction Table  ==\n");
    printf("#: predicted_level\n");
    for (int i = 0; i < (1 << (2*historyLength)); i++)
        printf ("0x%x: %d\n", i, prediction_table[pred_entry_idx][i]);
    printf("size = %lu\n", prediction_table[pred_entry_idx].size());

}

/******** loop **************/
LocPred_loop::LocPred_loop()
    : LocPred_BaseType(Loop),
      direction_history(default_pred_size, std::deque<bool>(1, true)),
      direction_history_sn(default_pred_size, std::deque<InstSeqNum>(1, 0)),
      loop_count_table(default_pred_size, 1),
      new_loop_count_table(default_pred_size, 0),
      pred_level_table(default_pred_size, Cache_L1),
      new_pred_level_table(default_pred_size, Cache_L1)
{
}

LocPred_loop::LocPred_loop(uint64_t pred_size)
    : LocPred_BaseType(Loop, pred_size),
      direction_history(pred_size, std::deque<bool>(1, true)),
      direction_history_sn(pred_size, std::deque<InstSeqNum>(1, 0)),
      loop_count_table(pred_size, 1),
      new_loop_count_table(pred_size, 0),
      pred_level_table(pred_size, Cache_L1),
      new_pred_level_table(pred_size, Cache_L1)
{
}

void
LocPred_loop::prepare(Addr load_PC, InstSeqNum load_sn)
{
    DPRINTF(LocPred, "loop: prepare(%lu, %ld)\n", load_PC, load_sn);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    direction_history[pred_entry_idx].push_front(Cache_L1);
    direction_history_sn[pred_entry_idx].push_front(load_sn);
}

CacheLevel_t
LocPred_loop::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    chosen_pred = Loop;

    DPRINTF(LocPred, "loop: predict(%lu, %ld)\n", load_PC, load_sn);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    // find the current loop length
    unsigned history_idx = 0;
    bool     M5_VAR_USED history_idx_valid = false;
    for (; history_idx < direction_history[pred_entry_idx].size(); history_idx++) {
        if (direction_history_sn[pred_entry_idx][history_idx] == load_sn) {
            history_idx_valid = true;
            break;
        }
    }
    assert (history_idx_valid);

    unsigned curr_loop_len = 1;
    for (; history_idx + curr_loop_len < direction_history[pred_entry_idx].size(); curr_loop_len++) {
        if (direction_history[pred_entry_idx][history_idx + curr_loop_len] == true) {
            break;
        }
    }

    CacheLevel_t pred;
    if (curr_loop_len % loop_count_table[pred_entry_idx] == 0) {
        pred = pred_level_table[pred_entry_idx];
    }
    else {
        pred = Cache_L1;
    }

    direction_history[pred_entry_idx][history_idx] = pred;

    return pred;
}

int
LocPred_loop::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    DPRINTF(LocPred, "loop: update(%lu, %ld, %ld)\n", load_PC, load_sn, actual_level);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    // find the corresponding entry
    unsigned history_idx = 0;
    bool     history_idx_valid = false;
    for (; history_idx < direction_history[pred_entry_idx].size(); history_idx++) {
        if (direction_history_sn[pred_entry_idx][history_idx] == load_sn) {
            history_idx_valid = true;
            break;
        }
    }

    if (!history_idx_valid)
        return 0;

    unsigned curr_loop_len = 1;
    for (; history_idx + curr_loop_len < direction_history[pred_entry_idx].size(); curr_loop_len++) {
        if (direction_history[pred_entry_idx][history_idx + curr_loop_len] == true) {
            break;
        }
    }

    // Update history
    bool actual_direction = (actual_level != Cache_L1);
    direction_history[pred_entry_idx][history_idx] = actual_direction;

    // Update the loop tables
    if (actual_direction) {
        if (loop_count_table[pred_entry_idx] != curr_loop_len) {
            if (new_loop_count_table[pred_entry_idx] == curr_loop_len)
                loop_count_table[pred_entry_idx] = curr_loop_len;
            else
                new_loop_count_table[pred_entry_idx] = curr_loop_len;
        }

        if (pred_level_table[pred_entry_idx] != actual_level) {
            if (new_pred_level_table[pred_entry_idx] == actual_level)
                pred_level_table[pred_entry_idx] = actual_level;
            else
                new_pred_level_table[pred_entry_idx] = actual_level;
        }
        // update_ValPred_on_Commit is TRUE if this is a non-L1
        return 1;
    }
    else {
        return 0;
    }
}

void
LocPred_loop::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
    DPRINTF(LocPred, "loop: commit(%lu, %ld, ifupdate=%d)\n", load_PC, load_sn, update_on_commit);

    if (update_on_commit) {
        unsigned pred_entry_idx = getLocalIndex(load_PC);
        while (!direction_history[pred_entry_idx].empty()) {
            if (direction_history_sn[pred_entry_idx].back() < load_sn) {
                direction_history[pred_entry_idx].pop_back();
                direction_history_sn[pred_entry_idx].pop_back();
            }
            else {
                break;
            }
        }
    }
}

void
LocPred_loop::squash(InstSeqNum squashed_sn)
{
    DPRINTF(LocPred, "loop: squash(%ld)\n", squashed_sn);

    for (int i = 0; i < predictorSize; i++) {
        while (!direction_history[i].empty()) {
            if (direction_history_sn[i].front() > squashed_sn) {
                direction_history[i].pop_front();
                direction_history_sn[i].pop_front();
            }
            else {
                break;
            }
        }
    }
}

void
LocPred_loop::print(Addr load_PC)
{
    unsigned pred_entry_idx = getLocalIndex(load_PC);

    printf("Pred data for 0x%lx\n", load_PC);

    printf("\n== <Loop> History Table  ==\n");
    printf("#: level | seqNum\n");
    for (int i = 0; i < direction_history[pred_entry_idx].size(); i++)
        printf ("%d: %d | %ld\n", i, direction_history[pred_entry_idx][i], direction_history_sn[pred_entry_idx][i]);
    printf("size = %lu\n", direction_history[pred_entry_idx].size());

    printf ("\n== <Loop> loop count  ==\n");
    printf ("curr: %d; new: %d\n", loop_count_table[pred_entry_idx], new_loop_count_table[pred_entry_idx]);

    printf ("\n== <Loop> Predicted level ==\n");
    printf ("curr: %d; new: %d\n", pred_level_table[pred_entry_idx], new_pred_level_table[pred_entry_idx]);
}

/**********************************************
 **           tournament - 2Way              **
 *********************************************/
LocPred_tournament_2Way::LocPred_tournament_2Way()
    : LocPred_BaseType(Tournament_2Way),
      pred1_conf(default_pred_size, 1 << (default_conf_length-1)),
      pred2_conf(default_pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
    assert(0);
}

LocPred_tournament_2Way::LocPred_tournament_2Way(uint64_t pred_size)
    : LocPred_BaseType(Tournament_2Way, pred_size),
      pred1_conf(pred_size, 1 << (default_conf_length-1)),
      pred2_conf(pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
    assert(0);
}

LocPred_tournament_2Way::LocPred_tournament_2Way(uint64_t pred_size, int conf_bit)
    : LocPred_BaseType(Tournament_2Way, pred_size),
      pred1_conf(pred_size, 1 << (conf_bit-1)),
      pred2_conf(pred_size, 1 << (conf_bit-1)),
      conf_max(1 << conf_bit)
{
    assert(0);
}

LocPred_tournament_2Way::LocPred_tournament_2Way(LocPred_t pred1_type, LocPred_t pred2_type, uint64_t pred_size)
    : LocPred_BaseType(Tournament_2Way, pred_size),
      pred1_conf(pred_size, 1 << (default_conf_length-1)),
      pred2_conf(pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
    pred1 = pred_type_to_pred(pred1_type);
    pred2 = pred_type_to_pred(pred2_type);
}

LocPred_tournament_2Way::LocPred_tournament_2Way(LocPred_t pred1_type, LocPred_t pred2_type, uint64_t pred_size, int conf_bit)
    : LocPred_BaseType(Tournament_2Way, pred_size),
      pred1_conf(pred_size, 1 << (conf_bit-1)),
      pred2_conf(pred_size, 1 << (conf_bit-1)),
      conf_max(1 << conf_bit)
{
    pred1 = pred_type_to_pred(pred1_type);
    pred2 = pred_type_to_pred(pred2_type);
}

LocPred_tournament_2Way::~LocPred_tournament_2Way()
{
    if (pred1)
        delete pred1;
    if (pred2)
        delete pred2;
}

void
LocPred_tournament_2Way::prepare(Addr load_PC, InstSeqNum load_sn)
{
    DPRINTF(LocPred, "tournament_2way: prepare(%lu, %ld)\n", load_PC, load_sn);
    pred1->prepare(load_PC, load_sn);
    pred2->prepare(load_PC, load_sn);
}

CacheLevel_t
LocPred_tournament_2Way::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    DPRINTF(LocPred, "tournament_2way: prepare(%lu, %ld)\n", load_PC, load_sn);

    CacheLevel_t pred1_prediction = pred1->predict(load_PC, load_sn, chosen_pred);
    CacheLevel_t pred2_prediction = pred2->predict(load_PC, load_sn, chosen_pred);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    int conf1 = pred1_conf[pred_entry_idx];
    int conf2 = pred2_conf[pred_entry_idx];

    if (conf1 >= conf2) {
        chosen_pred = pred1->getPredType();
        return pred1_prediction;
    }
    else {
        chosen_pred = pred2->getPredType();
        return pred2_prediction;
    }
}

int
LocPred_tournament_2Way::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    DPRINTF(LocPred, "tournament_2way: update(%lu, %ld, %ld)\n", load_PC, load_sn, actual_level);

    LocPred_t predtype;

    CacheLevel_t pred1_prediction = pred1->predict(load_PC, load_sn, predtype);
    CacheLevel_t pred2_prediction = pred2->predict(load_PC, load_sn, predtype);


    int pred1_update_on_commit = pred1->update(load_PC, load_sn, actual_level);
    int pred2_update_on_commit = pred2->update(load_PC, load_sn, actual_level);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    if (pred1_prediction == actual_level) {
        if (pred1_conf[pred_entry_idx] < conf_max)
            pred1_conf[pred_entry_idx]++;
    }
    else {
        if (pred1_conf[pred_entry_idx] > 0)
            pred1_conf[pred_entry_idx]--;
    }

    if (pred2_prediction == actual_level) {
        if (pred2_conf[pred_entry_idx] < conf_max)
            pred2_conf[pred_entry_idx]++;
    }
    else {
        if (pred2_conf[pred_entry_idx] > 0)
            pred2_conf[pred_entry_idx]--;
    }

    return (pred2_update_on_commit << 1) + pred1_update_on_commit;
}

void
LocPred_tournament_2Way::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
    int pred1_update_on_commit = update_on_commit & 0x1;
    int pred2_update_on_commit = (update_on_commit >> 1) & 0x1;

    DPRINTF(LocPred, "tournament_2way: update(%lu, %ld, update_pred1=%d, update_pred2=%d)\n",
            load_PC, load_sn, pred1_update_on_commit, pred2_update_on_commit);

    pred1->commit(load_PC, load_sn, pred1_update_on_commit);
    pred2->commit(load_PC, load_sn, pred2_update_on_commit);
}

void
LocPred_tournament_2Way::squash(InstSeqNum squashed_sn)
{
    pred1->squash(squashed_sn);
    pred2->squash(squashed_sn);
}

void
LocPred_tournament_2Way::print(Addr load_PC)
{
    pred1->print(load_PC);
    pred2->print(load_PC);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    printf("\nConf: pred1 = %d| pred2=%d\n",
            pred1_conf[pred_entry_idx], pred2_conf[pred_entry_idx]);
}


/**************************************************
 **             tournament - 3Way               ***
***************************************************/
LocPred_tournament_3Way::LocPred_tournament_3Way()
    : LocPred_BaseType(Tournament_3Way),
      pred1_conf(default_pred_size, 1 << (default_conf_length-1)),
      pred2_conf(default_pred_size, 1 << (default_conf_length-1)),
      pred3_conf(default_pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
}

LocPred_tournament_3Way::LocPred_tournament_3Way(uint64_t pred_size)
    : LocPred_BaseType(Tournament_3Way, pred_size),
      pred1_conf(pred_size, 1 << (default_conf_length-1)),
      pred2_conf(pred_size, 1 << (default_conf_length-1)),
      pred3_conf(pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
}

LocPred_tournament_3Way::LocPred_tournament_3Way(uint64_t pred_size, int conf_bit)
    : LocPred_BaseType(Tournament_3Way, pred_size),
      pred1_conf(pred_size, 1 << (conf_bit-1)),
      pred2_conf(pred_size, 1 << (conf_bit-1)),
      pred3_conf(pred_size, 1 << (conf_bit-1)),
      conf_max(1 << conf_bit)
{
}

LocPred_tournament_3Way::LocPred_tournament_3Way(LocPred_t pred1_type, LocPred_t pred2_type, LocPred_t pred3_type, uint64_t pred_size)
    : LocPred_BaseType(Tournament_3Way, pred_size),
      pred1_conf(pred_size, 1 << (default_conf_length-1)),
      pred2_conf(pred_size, 1 << (default_conf_length-1)),
      pred3_conf(pred_size, 1 << (default_conf_length-1)),
      conf_max(1 << default_conf_length)
{
    pred1 = pred_type_to_pred(pred1_type);
    pred2 = pred_type_to_pred(pred2_type);
    pred3 = pred_type_to_pred(pred3_type);
}

LocPred_tournament_3Way::LocPred_tournament_3Way(LocPred_t pred1_type, LocPred_t pred2_type, LocPred_t pred3_type, uint64_t pred_size, int conf_bit)
    : LocPred_BaseType(Tournament_3Way, pred_size),
      pred1_conf(pred_size, 1 << (conf_bit-1)),
      pred2_conf(pred_size, 1 << (conf_bit-1)),
      pred3_conf(pred_size, 1 << (conf_bit-1)),
      conf_max(1 << conf_bit)
{
    pred1 = pred_type_to_pred(pred1_type);
    pred2 = pred_type_to_pred(pred2_type);
    pred3 = pred_type_to_pred(pred3_type);
}

LocPred_tournament_3Way::~LocPred_tournament_3Way()
{
    if (pred1)
        delete pred1;
    if (pred2)
        delete pred2;
    if (pred3)
        delete pred3;
}

void
LocPred_tournament_3Way::prepare(Addr load_PC, InstSeqNum load_sn)
{
    DPRINTF(LocPred, "tournament_3way: prepare(%lu, %ld)\n", load_PC, load_sn);
    pred1->prepare(load_PC, load_sn);
    pred2->prepare(load_PC, load_sn);
    pred3->prepare(load_PC, load_sn);
}

CacheLevel_t
LocPred_tournament_3Way::predict(Addr load_PC, InstSeqNum load_sn, LocPred_t &chosen_pred)
{
    DPRINTF(LocPred, "tournament_3way: prepare(%lu, %ld)\n", load_PC, load_sn);

    CacheLevel_t pred1_prediction = pred1->predict(load_PC, load_sn, chosen_pred);
    CacheLevel_t pred2_prediction = pred2->predict(load_PC, load_sn, chosen_pred);
    CacheLevel_t pred3_prediction = pred3->predict(load_PC, load_sn, chosen_pred);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    int conf1 = pred1_conf[pred_entry_idx];
    int conf2 = pred2_conf[pred_entry_idx];
    int conf3 = pred3_conf[pred_entry_idx];

    if (conf1 >= conf2 && conf1 >= conf3) {
        chosen_pred = pred1->getPredType();
        return pred1_prediction;
    }
    else if (conf2 >= conf1 && conf2 >= conf3) {
        chosen_pred = pred2->getPredType();
        return pred2_prediction;
    }
    else {
        chosen_pred = pred3->getPredType();
        return pred3_prediction;
    }
}

int
LocPred_tournament_3Way::update(Addr load_PC, InstSeqNum load_sn, CacheLevel_t actual_level)
{
    DPRINTF(LocPred, "tournament_2way: update(%lu, %ld, %ld)\n", load_PC, load_sn, actual_level);

    LocPred_t predtype;

    CacheLevel_t pred1_prediction = pred1->predict(load_PC, load_sn, predtype);
    CacheLevel_t pred2_prediction = pred2->predict(load_PC, load_sn, predtype);
    CacheLevel_t pred3_prediction = pred3->predict(load_PC, load_sn, predtype);

    int pred1_update_on_commit = pred1->update(load_PC, load_sn, actual_level);
    int pred2_update_on_commit = pred2->update(load_PC, load_sn, actual_level);
    int pred3_update_on_commit = pred3->update(load_PC, load_sn, actual_level);

    unsigned pred_entry_idx = getLocalIndex(load_PC);

    if (pred1_prediction == actual_level) {
        if (pred1_conf[pred_entry_idx] < conf_max)
            pred1_conf[pred_entry_idx]++;
    }
    else {
        if (pred1_conf[pred_entry_idx] > 0)
            pred1_conf[pred_entry_idx]--;
    }

    if (pred2_prediction == actual_level) {
        if (pred2_conf[pred_entry_idx] < conf_max)
            pred2_conf[pred_entry_idx]++;
    }
    else {
        if (pred2_conf[pred_entry_idx] > 0)
            pred2_conf[pred_entry_idx]--;
    }

    if (pred3_prediction == actual_level) {
        if (pred3_conf[pred_entry_idx] < conf_max)
            pred3_conf[pred_entry_idx]++;
    }
    else {
        if (pred3_conf[pred_entry_idx] > 0)
            pred3_conf[pred_entry_idx]--;
    }

    return (pred3_update_on_commit << 2)  + (pred2_update_on_commit << 1) + pred1_update_on_commit;
}

void
LocPred_tournament_3Way::commit(Addr load_PC, InstSeqNum load_sn, int update_on_commit)
{
    int pred1_update_on_commit = update_on_commit & 0x1;
    int pred2_update_on_commit = (update_on_commit >> 1) & 0x1;
    int pred3_update_on_commit = (update_on_commit >> 2) & 0x1;

    DPRINTF(LocPred, "tournament_3way: update(%lu, %ld, update_pred1=%d, update_pred2=%d, update_pred3=%d)\n",
            load_PC, load_sn, pred1_update_on_commit, pred2_update_on_commit, pred3_update_on_commit);

    pred1->commit(load_PC, load_sn, pred1_update_on_commit);
    pred2->commit(load_PC, load_sn, pred2_update_on_commit);
    pred3->commit(load_PC, load_sn, pred3_update_on_commit);
}

void
LocPred_tournament_3Way::squash(InstSeqNum squashed_sn)
{
    pred1->squash(squashed_sn);
    pred2->squash(squashed_sn);
    pred3->squash(squashed_sn);
}

void
LocPred_tournament_3Way::print(Addr load_PC)
{
    pred1->print(load_PC);
    pred2->print(load_PC);
    pred3->print(load_PC);

    unsigned pred_entry_idx = getLocalIndex(load_PC);
    printf("\nConf: pred1 = %d | pred2 = %d | pred3 = %d\n",
            pred1_conf[pred_entry_idx], pred2_conf[pred_entry_idx], pred3_conf[pred_entry_idx]);
}




#endif // __CPU_O3_LOC_PRED_IMPL_HH__
