#!/usr/bin/env python

import llvmlite.binding as llvm
import time
import nativethread


llvm.initialize()
llvm.initialize_native_target()
llvm.initialize_native_asmprinter()


def get_engine():
    s = """; ModuleID = "__module__"
    target triple = "unknown-unknown-unknown"
    target datalayout = ""

    define void @run_forever() 
    {
    entry:
      br label %loop
    loop:
      br label %loop
    exit:
      ret void
    }
    
    define void @finish_now()
    {
    entry:
      ret void
    }
    """
    target = llvm.Target.from_triple(llvm.get_process_triple())
    target_machine = target.create_target_machine()
    mod = llvm.parse_assembly(s)
    mod.verify()
    engine = llvm.create_mcjit_compiler(mod, target_machine)
    engine.finalize_object()
    return engine


engine = get_engine()
busy_beaver_addr = engine.get_function_address('run_forever')
finish_quick_addr = engine.get_function_address('finish_now')

def ok_cbak(obj):
    print("finished OK!")

def cancel_cbak(obj):
    print("killed!")

def err_cbak(obj):
    print("I should never happen!")

thr_id_0 = nativethread.do_interruptibly(finish_quick_addr, ok_cbak, cancel_cbak, err_cbak, None)
thr_id_1 = nativethread.do_interruptibly(busy_beaver_addr, ok_cbak, cancel_cbak, err_cbak, None)

print("sleepy time now")
time.sleep(1)

nativethread.interrupt(thr_id_1)


