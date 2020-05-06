# Speculaive Data-Oblivious Execution (SDO) -- ISCA 2020

## 1. About SDO

Speculative Data Oblivious Execution (SDO) is a safe mechanism for improving speed of delay execution schemes (such as Speculative Taint Tracking (STT) (MICRO'19)) by executing leaky long-latency operations (such as loads) speculatively in a data-oblivious manner.
More details can be found in our ISCA'20 paper [here](). Here is a sample format for citing our work:
```

```

## 2. Implementing SDO

We implement SDO on top of [Gem5](ge5.org) simulator, which is a cycle-accurate simulator with both system call emulation and full system modes. SDO is implemented on an early version of Gem5 (commit:38a1e23). SDO is fully implemented on Gem5's O3 processor and Ruby memory subsystem.

SDO relies on STT to identify leakage (via tainting/untainting) and explicit/implicit channels. The major changes of SDO are:

* implementing a variety of location predictors 
* adding logic in load-store unit for issuing Obl-Ld requests for protecting unsafe loads
* adding logic in memory subsystem for implementing safe Obl-Ld operations at each cache level

## 3. Usafe

### 1) Build Gem5 executable

We use X86 architecture and Ruby memory model. To build the Gem5 executable (such as gem5.opt):

```
scons build/X86_MESI_Three_Level/gem5.opt
```

### 2) Run Gem5 executable

We provide a sample script in './sample_script' to run different configurations, including Unsafe baseline, STT and SDO.

### 3) Different SDO options

SDO supports different modes, and different predictor variants and configurations. Here we enumerate all important options:
* --scheme=[string]: different protection schemes
    * UnsafeBaseline: unmodified processor without any protection
    * DelayExecute: after identifying unsafe operations, delay the execution of those operations
    * SDO: after idenfitying unsafe operations, using SDO to execute those operations safely

* -- mem_model=[string]: memory consistency model
    * TSO: Total Store Ordering (TSO) model
    * RC: Released consistency (RC) model

* -- threat_model=[string]: attacker model
    * Spectre: Spectre threat model (covering control-flow speculation)
    * Futuristic: Futuristic threat model (covering all types of speculations, exceptions, interrupts)

* --STT: whether STT is enabled
    * (without this option): disable STT (all speculative transmitters are unsafe)
    * (with this option): enable STT (STT's taint tracking determines which transmtters are unsafe and requires protection)

* --enable_SDO: whether SDO is enabled
    * (without this option): disable SDO
    * (with this option): enable SDO

* --pred_type=[string]: location predictor type
    * static: static location predictor. Predict a constant level.
    * greedy: Greedy location predictor
    * hysteresis: Hysteresis location predictor
    * local: Local location predictor
    * loop: Loop location predictor
    * random: Random location predictor. Predict a random level.
    * perfect: Perfect location predictor. Predict the correct level.
    * tournament_2way: combining two predictors (e.g., greedy + hysteresis)
    * tournament_3way: combining three predictors (e.g., greedy + hysteresis + local)

* --pred_option=[number]: parameters for location predictor
    * (for static): 0 - always predicting L1; 1 - always predicting L2; 2 - always predicting L3 ; 3 - alwasy predicting DRAM
    * (for greedy, hysteresis, local, loop, perfect, tournament_2way/3way): 0 - can predict any level; 1 - don't predict DRAM; 2 - don't predict DRAM+LLC

* --subpred1_type=[string]: the first component of a tournament_2way/tournament_3way predictor
    * choose from any --pred_type options

* --subpred2_type=[string]: the second component of a tournament_2way/tournament_3way predictor
    * choose from any --pred_type options

* --subpred3_type=[string]: the third component of a tournament_3way predictor
    * choose from any --pred_type options

* --TLB_defense=[string]: TLB defense option
    * No: No TLB defense
    * SDO: SDO's TLB defense: on TLB misses, proceed with predicted page number
    * UnsafeDelay: on TLB misses, delay the memory access
