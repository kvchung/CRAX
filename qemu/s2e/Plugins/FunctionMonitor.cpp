extern "C" {
#include "config.h"
#include "qemu-common.h"
}

#include "FunctionMonitor.h"
#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>
#include <s2e/Utils.h>

#include <iostream>

namespace s2e {
namespace plugins {

S2E_DEFINE_PLUGIN(FunctionMonitor, "Function calls/returns monitoring plugin", "",);

void FunctionMonitor::initialize()
{
    s2e()->getCorePlugin()->onTranslateBlockEnd.connect(
            sigc::mem_fun(*this, &FunctionMonitor::slotTranslateBlockEnd));

    s2e()->getCorePlugin()->onTranslateJumpStart.connect(
            sigc::mem_fun(*this, &FunctionMonitor::slotTranslateJumpStart));

    if(s2e()->getConfig()->getBool(getConfigKey() + ".enableTracing")) {
        getCallSignal(0, 0)->connect(sigc::mem_fun(*this,
                                     &FunctionMonitor::slotTraceCall));
    }
}

FunctionMonitor::CallSignal* FunctionMonitor::getCallSignal(
                                uint64_t eip, uint64_t cr3)
{
    std::pair<CallDescriptorsMap::iterator, CallDescriptorsMap::iterator>
            range = m_callDescriptors.equal_range(eip);

    for(CallDescriptorsMap::iterator it = range.first; it != range.second; ++it) {
        if(it->second.cr3 == cr3)
            return &it->second.signal;
    }

    CallDescriptor descriptor = { cr3, CallSignal() };
    CallDescriptorsMap::iterator it =
            m_callDescriptors.insert(std::make_pair(eip, descriptor));
    return &it->second.signal;
}

void FunctionMonitor::slotTranslateBlockEnd(ExecutionSignal *signal,
                                      S2EExecutionState *state,
                                      TranslationBlock *tb,
                                      uint64_t pc, bool, uint64_t)
{
    /* We intercept all call and ret translation blocks */
    if (tb->s2e_tb_type == TB_CALL || tb->s2e_tb_type == TB_CALL_IND) {
        signal->connect(sigc::mem_fun(*this,
                            &FunctionMonitor::slotCall));
    }
}

void FunctionMonitor::slotTranslateJumpStart(ExecutionSignal *signal,
                                             S2EExecutionState *state,
                                             TranslationBlock *,
                                             uint64_t, int jump_type)
{
    if(jump_type == JT_RET || jump_type == JT_LRET) {
        signal->connect(sigc::mem_fun(*this,
                            &FunctionMonitor::slotRet));
    }
}

void FunctionMonitor::slotCall(S2EExecutionState *state, uint64_t pc)
{
    target_ulong cr3 = state->readCpuState(CPU_OFFSET(cr[3]), 8*sizeof(target_ulong));
    target_ulong eip = state->readCpuState(CPU_OFFSET(eip), 8*sizeof(target_ulong));

    target_ulong esp;
    bool ok = state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_ESP]),
                                             &esp, sizeof(target_ulong));
    if(!ok) {
        s2e()->getWarningsStream(state)
            << "Function call with symbolic ESP!" << std::endl
            << "  EIP=" << hexval(eip) << " CR3=" << hexval(cr3) << std::endl;
        return;
    }

    /* Issue signals attached to all calls (eip==0 means catch-all) */
    std::pair<CallDescriptorsMap::iterator, CallDescriptorsMap::iterator>
            range = m_callDescriptors.equal_range(0);
    for(CallDescriptorsMap::iterator it = range.first; it != range.second; ++it) {
        if(it->second.cr3 == 0 || it->second.cr3 == cr3) {
            ReturnDescriptor descriptor = { state, cr3, ReturnSignal() };
            it->second.signal.emit(state, &descriptor.signal);
            if(!descriptor.signal.empty()) {
                m_returnDescriptors.insert(std::make_pair(esp, descriptor));
            }
        }
    }

    /* Issue signals attached to specific calls */
    range = m_callDescriptors.equal_range(eip);
    for(CallDescriptorsMap::iterator it = range.first; it != range.second; ++it) {
        if(it->second.cr3 == 0 || it->second.cr3 == cr3) {
            ReturnDescriptor descriptor = { state, cr3, ReturnSignal() };
            it->second.signal.emit(state, &descriptor.signal);
            if(!descriptor.signal.empty()) {
                m_returnDescriptors.insert(std::make_pair(esp, descriptor));
            }
        }
    }
}

void FunctionMonitor::slotRet(S2EExecutionState *state, uint64_t pc)
{
    target_ulong cr3 = state->readCpuState(CPU_OFFSET(cr[3]), 8*sizeof(target_ulong));

    target_ulong esp;
    bool ok = state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_ESP]),
                                             &esp, sizeof(target_ulong));
    if(!ok) {
        target_ulong eip = state->readCpuState(CPU_OFFSET(eip),
                                               8*sizeof(target_ulong));
        s2e()->getWarningsStream(state)
            << "Function return with symbolic ESP!" << std::endl
            << "  EIP=" << hexval(eip) << " CR3=" << hexval(cr3) << std::endl;
        return;
    }

    bool finished = true;
    do {
        finished = true;
        std::pair<ReturnDescriptorsMap::iterator, ReturnDescriptorsMap::iterator>
                range = m_returnDescriptors.equal_range(esp);
        for(ReturnDescriptorsMap::iterator it = range.first; it != range.second; ++it) {
            if(it->second.state == state && it->second.cr3 == cr3) {
                it->second.signal.emit(state);
                m_returnDescriptors.erase(it);
                finished = false;
                break;
            }
        }
    } while(!finished);
}

void FunctionMonitor::slotTraceCall(S2EExecutionState *state, ReturnSignal *signal)
{
    static int f = 0;
    signal->connect(sigc::bind(sigc::mem_fun(*this, &FunctionMonitor::slotTraceRet), f));
    s2e()->getMessagesStream(state) << "Calling function " << f
                << " at " << hexval(state->getPc()) << std::endl;
    ++f;
}

void FunctionMonitor::slotTraceRet(S2EExecutionState *state, int f)
{
    s2e()->getMessagesStream(state) << "Returning from function "
                << f << std::endl;
}

} // namespace plugins
} // namespace s2e