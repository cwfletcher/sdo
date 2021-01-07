# Copyright (c) 2012, 2017 ARM Limited
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
# Authors: Andreas Sandberg

from m5 import fatal
import m5.objects
import inspect
import sys
from textwrap import TextWrapper

# Dictionary of mapping names of real CPU models to classes.
_cpu_classes = {}


def is_cpu_class(cls):
    """Determine if a class is a CPU that can be instantiated"""

    # We can't use the normal inspect.isclass because the ParamFactory
    # and ProxyFactory classes have a tendency to confuse it.
    try:
        return issubclass(cls, m5.objects.BaseCPU) and \
            not cls.abstract and \
            not issubclass(cls, m5.objects.CheckerCPU)
    except (TypeError, AttributeError):
        return False

def get(name):
    """Get a CPU class from a user provided class name or alias."""

    try:
        cpu_class = _cpu_classes[name]
        return cpu_class
    except KeyError:
        print "%s is not a valid CPU model." % (name,)
        sys.exit(1)

def print_cpu_list():
    """Print a list of available CPU classes including their aliases."""

    print "Available CPU classes:"
    doc_wrapper = TextWrapper(initial_indent="\t\t", subsequent_indent="\t\t")
    for name, cls in _cpu_classes.items():
        print "\t%s" % name

        # Try to extract the class documentation from the class help
        # string.
        doc = inspect.getdoc(cls)
        if doc:
            for line in doc_wrapper.wrap(doc):
                print line

def cpu_names():
    """Return a list of valid CPU names."""
    return _cpu_classes.keys()

def print_bp_list():
    """Print a list of available CPU classes including their aliases."""

    print "Available CPU classes:"
    doc_wrapper = TextWrapper(initial_indent="\t\t", subsequent_indent="\t\t")
    for name, cls in _cpu_classes.items():
        print "\t%s" % name

        # Try to extract the class documentation from the class help
        # string.
        doc = inspect.getdoc(cls)
        if doc:
            for line in doc_wrapper.wrap(doc):
                print line


# [SafeSpec] add knob to configure the CPU modes/simulation schemes
def config_scheme(cpu_cls, cpu_list, options):
    if issubclass(cpu_cls, m5.objects.DerivO3CPU):
        # Assign the same file name to all cpus for now.
        if options.scheme == None or options.mem_model == None:
            fatal("Need to provide scheme and mem_model to run simulation with DerivO3CPU")

        if options.scheme == "UnsafeBaseline":
            print "**********"
            print "info: Configure for DerivO3CPU. scheme=%s; mem_model=%s, mshr_size=%d"\
                % (options.scheme, options.mem_model, options.MSHR_size)
            print "**********"
        else:
            print "**********"
            print "info: Configure for DerivO3CPU. scheme=%s; mem_model=%s, MSHR_size=%d"\
                % (options.scheme, options.mem_model, options.MSHR_size)

            print "STT=%d; impChannel=%d"\
                % (options.STT, options.impChannel)
            print "**********"

        for cpu in cpu_list:
            if len(options.scheme) != 0:
                cpu.scheme = options.scheme

            if len(options.mem_model) != 0:
                cpu.mem_model = options.mem_model

            if options.threat_model:
                cpu.threat_model = options.threat_model

            ## Jiyong, STT: STT related configurations passed to gem5 O3CPU
            if options.STT:
                cpu.STT = True;
            else:
                cpu.STT = False;

            if options.impChannel:
                cpu.impChannel = True;
            else:
                cpu.impChannel = False;

            if options.moreTransTypes:
                cpu.moreTransTypes = options.moreTransTypes
            else:
                cpu.moreTransTypes = 0

            if options.ifPrintROB:
                cpu.ifPrintROB = True;
            else:
                cpu.ifPrintROB = False;

            ## Jiyong, STT: STT-profiling related configurations passed to genHitHistogramem5 O3CPU
            if options.enable_STT_overhead_profiling:
                cpu.enable_STT_overhead_profiling = True
            else:
                cpu.enable_STT_overhead_profiling = False

            if options.enable_loadhit_trace_profiling:
                cpu.enable_loadhit_trace_profiling = True
            else:
                cpu.enable_loadhit_trace_profiling = False

            if options.genHitTrace_NumLoads:
                cpu.genHitTrace_NumLoads = options.genHitTrace_NumLoads
            else:
                cpu.genHitTrace_NumLoads = 0

            if options.genHitTrace_WkldName:
                cpu.genHitTrace_WkldName = options.genHitTrace_WkldName

            ## Jiyong, SDO configs
            if options.scheme == "SDO":
                cpu.enable_MLDOM = True
                print "### SDO is enabled"
            else:
                cpu.enable_MLDOM = False
                print "### SDO is disabled"

            if options.pred_type:
                assert (cpu.enable_MLDOM)
                cpu.pred_type = options.pred_type
                print "### SDO predictor is: %s" % options.pred_type
            else:
                assert not cpu.enable_MLDOM
                cpu.pred_type = "none"

            if options.pred_option:
                assert (cpu.enable_MLDOM)
                cpu.pred_option = options.pred_option
                print "### SDO predictor has option: %d" % options.pred_option

            if options.subpred1_type:
                assert (cpu.enable_MLDOM)
                cpu.tournament_pred1_type = options.subpred1_type
            else:
                cpu.tournament_pred1_type = "none"

            if options.subpred2_type:
                assert (cpu.enable_MLDOM)
                cpu.tournament_pred2_type = options.subpred2_type
            else:
                cpu.tournament_pred2_type = "none"

            if options.subpred3_type:
                assert (cpu.enable_MLDOM)
                cpu.tournament_pred3_type = options.subpred3_type
            else:
                cpu.tournament_pred3_type = "none"

            cpu.expose_only = options.expose_only
            cpu.disable_2ndld = options.disable_2ndld
            cpu.TLB_defense = options.TLB_defense
            cpu.enable_OblS_contention = options.enable_OblS_contention

    else:
        print "not DerivO3CPU"



def config_etrace(cpu_cls, cpu_list, options):
    if issubclass(cpu_cls, m5.objects.DerivO3CPU):
        # Assign the same file name to all cpus for now. This must be
        # revisited when creating elastic traces for multi processor systems.
        for cpu in cpu_list:
            # Attach the elastic trace probe listener. Set the protobuf trace
            # file names. Set the dependency window size equal to the cpu it
            # is attached to.
            cpu.traceListener = m5.objects.ElasticTrace(
                                instFetchTraceFile = options.inst_trace_file,
                                dataDepTraceFile = options.data_trace_file,
                                depWindowSize = 3 * cpu.numROBEntries)
            # Make the number of entries in the ROB, LQ and SQ very
            # large so that there are no stalls due to resource
            # limitation as such stalls will get captured in the trace
            # as compute delay. For replay, ROB, LQ and SQ sizes are
            # modelled in the Trace CPU.
            cpu.numROBEntries = 512;
            cpu.LQEntries = 128;
            cpu.SQEntries = 128;
    else:
        fatal("%s does not support data dependency tracing. Use a CPU model of"
              " type or inherited from DerivO3CPU.", cpu_cls)

# Add all CPUs in the object hierarchy.
for name, cls in inspect.getmembers(m5.objects, is_cpu_class):
    _cpu_classes[name] = cls


from m5.defines import buildEnv
from importlib import import_module
for package in [ "generic", buildEnv['TARGET_ISA']]:
    try:
        package = import_module(".cores." + package, package=__package__)
    except ImportError:
        # No timing models for this ISA
        continue

    for mod_name, module in inspect.getmembers(package, inspect.ismodule):
        for name, cls in inspect.getmembers(module, is_cpu_class):
            _cpu_classes[name] = cls
