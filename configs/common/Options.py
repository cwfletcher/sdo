# Copyright (c) 2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Lisa Hsu

from __future__ import print_function
from __future__ import absolute_import

import m5
from m5.defines import buildEnv
from m5.objects import *
from common.Benchmarks import *
from . import ObjectList

def _listCpuTypes(option, opt, value, parser):
    ObjectList.cpu_list.print()
    sys.exit(0)

def _listBPTypes(option, opt, value, parser):
    ObjectList.bp_list.print()
    sys.exit(0)

def _listHWPTypes(option, opt, value, parser):
    ObjectList.hwp_list.print()
    sys.exit(0)

def _listIndirectBPTypes(option, opt, value, parser):
    ObjectList.indirect_bp_list.print()
    sys.exit(0)

def _listMemTypes(option, opt, value, parser):
    ObjectList.mem_list.print()
    sys.exit(0)

def _listPlatformTypes(option, opt, value, parser):
    ObjectList.platform_list.print()
    sys.exit(0)

# Add the very basic options that work also in the case of the no ISA
# being used, and consequently no CPUs, but rather various types of
# testers and traffic generators.
def addNoISAOptions(parser):
    parser.add_option("-n", "--num-cpus", type="int", default=1)
    parser.add_option("--sys-voltage", action="store", type="string",
                      default='1.0V',
                      help = """Top-level voltage for blocks running at system
                      power supply""")
    parser.add_option("--sys-clock", action="store", type="string",
                      default='1GHz',
                      help = """Top-level clock for blocks running at system
                      speed""")

    # Memory Options
    parser.add_option("--list-mem-types",
                      action="callback", callback=_listMemTypes,
                      help="List available memory types")
    parser.add_option("--mem-type", type="choice", default="DDR3_1600_8x8",
                      choices=ObjectList.mem_list.get_names(),
                      help = "type of memory to use")
    parser.add_option("--mem-channels", type="int", default=1,
                      help = "number of memory channels")
    parser.add_option("--mem-ranks", type="int", default=None,
                      help = "number of memory ranks per channel")
    parser.add_option("--mem-size", action="store", type="string",
                      default="512MB",
                      help="Specify the physical memory size (single memory)")


    parser.add_option("--memchecker", action="store_true")

    # Cache Options
    parser.add_option("--external-memory-system", type="string",
                      help="use external ports of this port_type for caches")
    parser.add_option("--tlm-memory", type="string",
                      help="use external port for SystemC TLM cosimulation")
    parser.add_option("--caches", action="store_true")
    parser.add_option("--l2cache", action="store_true")
    parser.add_option("--num-dirs", type="int", default=1)
    parser.add_option("--cacheline_size", type="int", default=64)
    
    # For MESI_Two_Level:
    # parser.add_option("--num-l2caches", type="int", default=1)
    # parser.add_option("--num-l3caches", type="int", default=1)
    parser.add_option("--l1d_size", type="string", default="64kB")
    parser.add_option("--l1i_size", type="string", default="32kB")
    # parser.add_option("--l2_size", type="string", default="2MB")
    # parser.add_option("--l3_size", type="string", default="16MB")
    parser.add_option("--l1d_assoc", type="int", default=2)
    parser.add_option("--l1i_assoc", type="int", default=2)
    # parser.add_option("--l2_assoc", type="int", default=8)
    # parser.add_option("--l3_assoc", type="int", default=16)

    # For MESI_Three_Level:
    parser.add_option("--num-l2caches", type="int", default=1)
    parser.add_option("--l0i_assoc", type="int", default=8)
    parser.add_option("--l0i_size", type="string", default="32kB")
    parser.add_option("--l0i_tag_latency", type="int", default=2)
    parser.add_option("--l0i_data_latency", type="int", default=2)
    parser.add_option("--l0d_assoc", type="int", default=8)
    parser.add_option("--l0d_size", type="string", default="32kB")
    parser.add_option("--l0d_tag_latency", type="int", default=2)
    parser.add_option("--l0d_data_latency", type="int", default=2)
    parser.add_option("--l1_assoc", type="int", default=8)
    parser.add_option("--l1_size", type="string", default="256kB")
    parser.add_option("--l1_tag_latency", type="int", default=6)  # 3 is the best
    parser.add_option("--l1_data_latency", type="int", default=6) # 3 is the best
    parser.add_option("--l2_assoc", type="int", default=8)
    parser.add_option("--l2_size", type="string", default="2MB")
    parser.add_option("--l2_tag_latency", type="int", default=15)
    parser.add_option("--l2_data_latency", type="int", default=15)
    parser.add_option("--num_banks", type="int", default=1)
    parser.add_option("--MSHR_size", type="int", default=16)


    # Enable Ruby
    parser.add_option("--ruby", action="store_true")

    # Run duration options
    parser.add_option("-m", "--abs-max-tick", type="int", default=m5.MaxTick,
                      metavar="TICKS", help="Run to absolute simulated tick "
                      "specified including ticks from a restored checkpoint")
    parser.add_option("--rel-max-tick", type="int", default=None,
                      metavar="TICKS", help="Simulate for specified number of"
                      " ticks relative to the simulation start tick (e.g. if "
                      "restoring a checkpoint)")
    parser.add_option("--maxtime", type="float", default=None,
                      help="Run to the specified absolute simulated time in "
                      "seconds")

# Add common options that assume a non-NULL ISA.
def addCommonOptions(parser):
    # start by adding the base options that do not assume an ISA
    addNoISAOptions(parser)

    # system options
    parser.add_option("--list-cpu-types",
                      action="callback", callback=_listCpuTypes,
                      help="List available CPU types")
    parser.add_option("--cpu-type", type="choice", default="AtomicSimpleCPU",
                      choices=ObjectList.cpu_list.get_names(),
                      help = "type of cpu to run with")
    parser.add_option("--list-bp-types",
                      action="callback", callback=_listBPTypes,
                      help="List available branch predictor types")
    parser.add_option("--list-indirect-bp-types",
                      action="callback", callback=_listIndirectBPTypes,
                      help="List available indirect branch predictor types")
    parser.add_option("--bp-type", type="choice", default=None,
                      choices=ObjectList.bp_list.get_names(),
                      help="""
                      types of branch predictor to run with
                      (if not set, use the default branch predictor of
                      the selected CPU)""")
    parser.add_option("--indirect-bp-type", type="choice",
                      default="SimpleIndirectPredictor",
                      choices=ObjectList.indirect_bp_list.get_names(),
                      help="types of indirect branch predictor to run with")
    parser.add_option("--list-hwp-types",
                      action="callback", callback=_listHWPTypes,
                      help="List available hardware prefetcher types")
    parser.add_option("--l1i-hwp-type", type="choice", default=None,
                      choices=ObjectList.hwp_list.get_names(),
                      help = """
                      type of hardware prefetcher to use with the L1
                      instruction cache.
                      (if not set, use the default prefetcher of
                      the selected cache)""")
    parser.add_option("--l1d-hwp-type", type="choice", default=None,
                      choices=ObjectList.hwp_list.get_names(),
                      help = """
                      type of hardware prefetcher to use with the L1
                      data cache.
                      (if not set, use the default prefetcher of
                      the selected cache)""")
    parser.add_option("--l2-hwp-type", type="choice", default=None,
                      choices=ObjectList.hwp_list.get_names(),
                      help = """
                      type of hardware prefetcher to use with the L2 cache.
                      (if not set, use the default prefetcher of
                      the selected cache)""")
    parser.add_option("--checker", action="store_true");
    parser.add_option("--cpu-clock", action="store", type="string",
                      default='2GHz',
                      help="Clock for blocks running at CPU speed")
    parser.add_option("--smt", action="store_true", default=False,
                      help = """
                      Only used if multiple programs are specified. If true,
                      then the number of threads per cpu is same as the
                      number of programs.""")
    parser.add_option("--elastic-trace-en", action="store_true",
                      help="""Enable capture of data dependency and instruction
                      fetch traces using elastic trace probe.""")
    # Trace file paths input to trace probe in a capture simulation and input
    # to Trace CPU in a replay simulation
    parser.add_option("--inst-trace-file", action="store", type="string",
                      help="""Instruction fetch trace file input to
                      Elastic Trace probe in a capture simulation and
                      Trace CPU in a replay simulation""", default="")
    parser.add_option("--data-trace-file", action="store", type="string",
                      help="""Data dependency trace file input to
                      Elastic Trace probe in a capture simulation and
                      Trace CPU in a replay simulation""", default="")

    parser.add_option("-l", "--lpae", action="store_true")
    parser.add_option("-V", "--virtualisation", action="store_true")

    parser.add_option("--fastmem", action="store_true")

    # dist-gem5 options
    parser.add_option("--dist", action="store_true",
                      help="Parallel distributed gem5 simulation.")
    parser.add_option("--dist-sync-on-pseudo-op", action="store_true",
                      help="Use a pseudo-op to start dist-gem5 synchronization.")
    parser.add_option("--is-switch", action="store_true",
                      help="Select the network switch simulator process for a"\
                      "distributed gem5 run")
    parser.add_option("--dist-rank", default=0, action="store", type="int",
                      help="Rank of this system within the dist gem5 run.")
    parser.add_option("--dist-size", default=0, action="store", type="int",
                      help="Number of gem5 processes within the dist gem5 run.")
    parser.add_option("--dist-server-name",
                      default="127.0.0.1",
                      action="store", type="string",
                      help="Name of the message server host\nDEFAULT: localhost")
    parser.add_option("--dist-server-port",
                      default=2200,
                      action="store", type="int",
                      help="Message server listen port\nDEFAULT: 2200")
    parser.add_option("--dist-sync-repeat",
                      default="0us",
                      action="store", type="string",
                      help="Repeat interval for synchronisation barriers among dist-gem5 processes\nDEFAULT: --ethernet-linkdelay")
    parser.add_option("--dist-sync-start",
                      default="5200000000000t",
                      action="store", type="string",
                      help="Time to schedule the first dist synchronisation barrier\nDEFAULT:5200000000000t")
    parser.add_option("--ethernet-linkspeed", default="10Gbps",
                        action="store", type="string",
                        help="Link speed in bps\nDEFAULT: 10Gbps")
    parser.add_option("--ethernet-linkdelay", default="10us",
                      action="store", type="string",
                      help="Link delay in seconds\nDEFAULT: 10us")

    # Run duration options
    parser.add_option("-I", "--maxinsts", action="store", type="int",
                      default=None, help="""Total number of instructions to
                                            simulate (default: run forever)""")
    parser.add_option("--work-item-id", action="store", type="int",
                      help="the specific work id for exit & checkpointing")
    parser.add_option("--num-work-ids", action="store", type="int",
                      help="Number of distinct work item types")
    parser.add_option("--work-begin-cpu-id-exit", action="store", type="int",
                      help="exit when work starts on the specified cpu")
    parser.add_option("--work-end-exit-count", action="store", type="int",
                      help="exit at specified work end count")
    parser.add_option("--work-begin-exit-count", action="store", type="int",
                      help="exit at specified work begin count")
    parser.add_option("--init-param", action="store", type="int", default=0,
                      help="""Parameter available in simulation with m5
                              initparam""")
    parser.add_option("--initialize-only", action="store_true", default=False,
                      help="""Exit after initialization. Do not simulate time.
                              Useful when gem5 is run as a library.""")

    # Simpoint options
    parser.add_option("--simpoint-profile", action="store_true",
                      help="Enable basic block profiling for SimPoints")
    parser.add_option("--simpoint-interval", type="int", default=10000000,
                      help="SimPoint interval in num of instructions")
    parser.add_option("--take-simpoint-checkpoints", action="store", type="string",
        help="<simpoint file,weight file,interval-length,warmup-length>")
    parser.add_option("--restore-simpoint-checkpoint", action="store_true",
        help="restore from a simpoint checkpoint taken with " +
             "--take-simpoint-checkpoints")
    parser.add_option("--simpt-ckpt", action="store", default=None, type="int")

    # Checkpointing options
    ###Note that performing checkpointing via python script files will override
    ###checkpoint instructions built into binaries.
    parser.add_option("--take-checkpoints", action="store", type="string",
        help="<M,N> take checkpoints at tick M and every N ticks thereafter")
    parser.add_option("--max-checkpoints", action="store", type="int",
        help="the maximum number of checkpoints to drop", default=5)
    parser.add_option("--checkpoint-dir", action="store", type="string",
        help="Place all checkpoints in this absolute directory")
    parser.add_option("-r", "--checkpoint-restore", action="store", type="int",
        help="restore from checkpoint <N>")
    parser.add_option("--checkpoint-at-end", action="store_true",
                      help="take a checkpoint at end of run")
    parser.add_option("--work-begin-checkpoint-count", action="store", type="int",
                      help="checkpoint at specified work begin count")
    parser.add_option("--work-end-checkpoint-count", action="store", type="int",
                      help="checkpoint at specified work end count")
    parser.add_option("--work-cpus-checkpoint-count", action="store", type="int",
                      help="checkpoint and exit when active cpu count is reached")
    parser.add_option("--restore-with-cpu", action="store", type="choice",
                      default="AtomicSimpleCPU",
                      choices=ObjectList.cpu_list.get_names(),
                      help = "cpu type for restoring from a checkpoint")


    # CPU Switching - default switch model goes from a checkpoint
    # to a timing simple CPU with caches to warm up, then to detailed CPU for
    # data measurement
    parser.add_option("--repeat-switch", action="store", type="int",
        default=None,
        help="switch back and forth between CPUs with period <N>")
    parser.add_option("-s", "--standard-switch", action="store", type="int",
        default=None,
        help="switch from timing to Detailed CPU after warmup period of <N>")
    parser.add_option("-p", "--prog-interval", type="str",
        help="CPU Progress Interval")

    # Fastforwarding and simpoint related materials
    parser.add_option("-W", "--warmup-insts", action="store", type="int",
        default=None,
        help="Warmup period in total instructions (requires --standard-switch)")
    parser.add_option("--bench", action="store", type="string", default=None,
        help="base names for --take-checkpoint and --checkpoint-restore")
    parser.add_option("-F", "--fast-forward", action="store", type="string",
        default=None,
        help="Number of instructions to fast forward before switching")
    parser.add_option("--fast-forward-pseudo-inst", action="store_true",
        default=False,
        help="Fast forward before switching until hitting switch_cup insts")
    parser.add_option("-S", "--simpoint", action="store_true", default=False,
        help="""Use workload simpoints as an instruction offset for
                --checkpoint-restore or --take-checkpoint.""")
    parser.add_option("--at-instruction", action="store_true", default=False,
        help="""Treat value of --checkpoint-restore or --take-checkpoint as a
                number of instructions.""")
    parser.add_option("--spec-input", default="ref", type="choice",
                      choices=["ref", "test", "train", "smred", "mdred",
                               "lgred"],
                      help="Input set size for SPEC CPU2000 benchmarks.")
    parser.add_option("--arm-iset", default="arm", type="choice",
                      choices=["arm", "thumb", "aarch64"],
                      help="ARM instruction set.")
    # [SafeSpec] add options to configure needsTSO and scheme
    parser.add_option("--scheme", default=None, action="store", type="choice",
            choices=["UnsafeBaseline", "DelayExecute", "SDO"],
            help="choose baseline or defense designs to evaluate")
    parser.add_option("--mem_model", default=None, action="store", type="choice",
            choices=["TSO", "RC"],
            help="Select TSO or RC.")
    parser.add_option("--threat_model", default=None, action="store", type="choice",
            choices=["Spectre", "Futuristic"],
            help="Select Spectre of Futuristic.")
    # [Jiyong,STT] add options for STT configurations
    parser.add_option("--STT", action="store_true", default=False,
            help="Whether using STT.")
    parser.add_option("--impChannel", default=None, action="store", type="int",
            help="implicit channel mechanism")
    parser.add_option("--ifPrintROB", default=None, action="store", type="int",
            help="Enable printing ROB content at every cycle")
    parser.add_option("--moreTransTypes", default=0, action="store", type="int",
            help="Include more transmit instruction types.")
    # [Jiyong,STT_SDO] add options for reading loads to dump trace
    parser.add_option("--enable_STT_overhead_profiling", default=None, action="store", type="int",
            help="print out STT-delay related stats for all loads")
    parser.add_option("--enable_loadhit_trace_profiling", default=None, action="store", type="int",
            help="print out cache-hit trace for specified loads")
    parser.add_option("--genHitTrace_NumLoads", default=0, action="store", type="int",
            help="generate a hit trace for specfied loads. number of Ld specified by this option. Need to read a file for specified load")
    parser.add_option("--genHitTrace_WkldName", default=None, action="store", type="string",
            help="the name of the workload to generate hit trace. Must have genHitTraceForLoad > 0")
    # [Jiyong,STT_SDO] add options to enable SDO
    parser.add_option("--enable_SDO", action="store_true", default=False,
            help="enable SDO") 
    # [Jiyong,STT_SDO] add options for configuring location (cache) predictor
    ## static: access cache levels from L1 -> LX (X is specified as a parameter)
    ##      0: L0 (L1), 1: L1 (L2), 2: L2(LLC), 3: Mem(DRAM)
    ## Random: pick a random level (level is generated by a random generator)
    ## Perfect:
    ##      Safe: access all cache levels until the block is found. Still safe because it doesn't wait for transient coherence state (cache misses are possible)
    ##      Unsafe: access all cache levels until the block is found. Not safe because it wait for transient coherence state (cache misses are impossible) (oblS becomes normal ld request)
    ## <all configs below are dynamic predictors>
    ##      pred_option = 1X/2X: turn on "delay_on_DRAM_pred" to disable prediction to DRAM (if DRAM is predicted, wait)
    ##      pred_option = 2X:    turn on "delay_on_LLC_pred" to disable prediction to LLC (if LLC is predicted, wait)
    ##      also set the predictor-specific bit
    ## greedy:     dynamic greedy-style predictor
    ## hysteresis: dynamic hysteresis-style predictor
    ## local:      dynamic local predictor
    ## loop:       dynamic loop predictor
    ## tournament_2way: combine two dynamic predictors. Currently supported:
    ##      static + local
    ##      static + greedy
    ##      static + loop
    ##      local + greedy
    ##      local + loop
    ##      greedy + loop
    ##      greedy + hysteresis
    ## tournament_3way: combine three dynamic predictors. Currently supported:
    ##      static + local + loop
    ##      static + greedy + loop
    parser.add_option("--pred_type", default=None, action="store", type="choice",
            choices=["static", "random", "perfect", "greedy", "hysteresis", "local", "loop", "tournament_2way", "tournament_3way"],
            help="choice for location predictor")
    parser.add_option("--subpred1_type", default=None, action="store", type="choice",
            choices=["static", "greedy", "hysteresis", "local", "loop", "None"],
            help="choice for the 1st component of the tournament predictor")
    parser.add_option("--subpred2_type", default=None, action="store", type="choice",
            choices=["static", "greedy", "hysteresis", "local", "loop", "None"],
            help="choice for the 2nd component of the tournament predictor")
    parser.add_option("--subpred3_type", default=None, action="store", type="choice",
            choices=["static", "greedy", "hysteresis", "local", "loop", "None"],
            help="choice for the 3rd component of the tournament predictor")

    parser.add_option("--pred_option", default=0, action="store", type="int",
            help="option for specified predictor. For static: option = predicted level(0,1,2,3); For static predictor: option = 0-normal, 1-No DRAM, 2-No DRAM+LLC")
    ## ExposeOnly: only for single thread workloads: Validation -> Expose on OblS hit
    parser.add_option("--expose_only", default=0, action="store", type="int",
            help="replace obls hit validation with exposure. Only enabled if single thread")
    ## No2ndLd:    don't perform the second load (when 2nd ld, aka exposure/validation, is optional)
    parser.add_option("--disable_2ndld", default=0, action="store", type="int",
            help="remove the 2nd ld (exposure/validation) on obls hit")

    # [Jiyong,STT_SDO] add options for TLB protection
    ## No:          doesn't protect side-channel against TLB
    ## DelayPTWalk: InvisiSpec's TLB protection mechanism: delay on spec TLB miss. Still unsafe
    ## SDO:         SDO's protection for TLB: no delay on Spec TLB miss, squash later
    parser.add_option("--TLB_defense", default=None, action="store", type="choice",
            choices=["No", "SafeDelay", "UnsafeDelay", "SDO"],
            help="choices for TLB protection mechanism")
    parser.add_option("--enable_OblS_contention", default=0, action="store", type="int",
            help="enable obls contention [Unsafe, only for testing]")

    parser.add_option("--ruby_enable_resource_stall", default=1, action="store", type="int",
            help="enable tag/data array access latency")


def addSEOptions(parser):
    # Benchmark options
    parser.add_option("-c", "--cmd", default="",
                      help="The binary to run in syscall emulation mode.")
    parser.add_option("-o", "--options", default="",
                      help="""The options to pass to the binary, use " "
                              around the entire string""")
    parser.add_option("-e", "--env", default="",
                      help="Initialize workload environment from text file.")
    parser.add_option("-i", "--input", default="",
                      help="Read stdin from a file.")
    parser.add_option("--output", default="",
                      help="Redirect stdout to a file.")
    parser.add_option("--errout", default="",
                      help="Redirect stderr to a file.")

def addFSOptions(parser):
    from FSConfig import os_types

    # Simulation options
    parser.add_option("--timesync", action="store_true",
            help="Prevent simulated time from getting ahead of real time")

    # System options
    parser.add_option("--kernel", action="store", type="string")
    parser.add_option("--os-type", action="store", type="choice",
            choices=os_types[buildEnv['TARGET_ISA']], default="linux",
            help="Specifies type of OS to boot")
    parser.add_option("--script", action="store", type="string")
    parser.add_option("--frame-capture", action="store_true",
            help="Stores changed frame buffers from the VNC server to compressed "\
            "files in the gem5 output directory")

    if buildEnv['TARGET_ISA'] == "arm":
        parser.add_option("--bare-metal", action="store_true",
                   help="Provide the raw system without the linux specific bits")
        parser.add_option("--list-machine-types",
                          action="callback", callback=_listPlatformTypes,
                      help="List available platform types")
        parser.add_option("--machine-type", action="store", type="choice",
                choices=ObjectList.platform_list.get_names(),
                default="VExpress_EMM")
        parser.add_option("--dtb-filename", action="store", type="string",
              help="Specifies device tree blob file to use with device-tree-"\
              "enabled kernels")
        parser.add_option("--enable-security-extensions", action="store_true",
              help="Turn on the ARM Security Extensions")
        parser.add_option("--enable-context-switch-stats-dump", \
                action="store_true", help="Enable stats dump at context "\
                "switches and dump tasks file (required for Streamline)")
        parser.add_option("--generate-dtb", action="store_true", default=False,
                    help="Automatically generate a dtb file")

    # Benchmark options
    parser.add_option("--dual", action="store_true",
                      help="Simulate two systems attached with an ethernet link")
    parser.add_option("-b", "--benchmark", action="store", type="string",
                      dest="benchmark",
                      help="Specify the benchmark to run. Available benchmarks: %s"\
                      % DefinedBenchmarks)

    # Metafile options
    parser.add_option("--etherdump", action="store", type="string", dest="etherdump",
                      help="Specify the filename to dump a pcap capture of the" \
                      "ethernet traffic")

    # Disk Image Options
    parser.add_option("--disk-image", action="store", type="string", default=None,
                      help="Path to the disk image to use.")
    parser.add_option("--root-device", action="store", type="string", default=None,
                      help="OS device name for root partition")

    # Command line options
    parser.add_option("--command-line", action="store", type="string",
                      default=None,
                      help="Template for the kernel command line.")
    parser.add_option("--command-line-file", action="store",
                      default=None, type="string",
                      help="File with a template for the kernel command line")
