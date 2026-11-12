#include <kubridge.h>
#include <psp2/kernel/threadmgr.h>

#include "hw/sh4/dyna/blockmanager.h"

#include "oslib/host_context.h"

#include "hw/sh4/dyna/ngen.h"
#include "rend/TexCache.h"
#include "hw/mem/addrspace.h"
#include "hw/mem/mem_watch.h"

static KuKernelAbortHandler nextHandler;
static __thread int threadInFault = 0;

void context_to_segfault(host_context_t *hostctx, void *segfault_ctx)
{
	KuKernelAbortContext *abortContext = reinterpret_cast<KuKernelAbortContext *>(segfault_ctx);
	for (int i = 0; i < 15; i++)
		(&abortContext->r0)[i] = hostctx->reg[i];

	abortContext->pc = hostctx->pc;
}
void context_from_segfault(host_context_t *hostctx, void *segfault_ctx)
{
	KuKernelAbortContext *abortContext = reinterpret_cast<KuKernelAbortContext *>(segfault_ctx);
	for (int i = 0; i < 15; i++)
		hostctx->reg[i] = (&abortContext->r0)[i];

	hostctx->pc = abortContext->pc;
}

// Vita test mode: memory faults per kind, read and reset by the fps log
unsigned diag_fault_code = 0, diag_fault_vram = 0, diag_fault_fpcb = 0, diag_fault_rewrite = 0;

void abortHandler(KuKernelAbortContext *abortContext)
{
	// Ram watcher for net rollback
	if (memwatch::writeAccess((u8 *)abortContext->FAR))
		return;
	// code protection in RAM
	if (bm_RamWriteAccess((u8*)abortContext->FAR)) {
		diag_fault_code++;
		return;
	}
	// texture protection in VRAM
	if (VramLockedWrite((u8*)abortContext->FAR)) {
		diag_fault_vram++;
		return;
	}
	// FPCB jump table protection
	if (addrspace::bm_lockedWrite((u8*)abortContext->FAR)) {
		diag_fault_fpcb++;
		return;
	}

#if FEAT_SHREC == DYNAREC_JIT
	// fast mem access rewriting
	host_context_t ctx;
	context_from_segfault(&ctx, abortContext);
	if (sh4Dynarec->rewrite(ctx, (void *)abortContext->FAR))
	{
		context_to_segfault(&ctx, abortContext);
		diag_fault_rewrite++;
		return;
	}
#endif
	// The pages being remapped can stay unreachable for hundreds of milliseconds at start, and the fault
	// can hit this handler's own callees: wait and retry instead of giving up at the first nested fault.
	if (threadInFault < 50)
	{
		threadInFault++;
		if (threadInFault == 1)
			ERROR_LOG(COMMON, "Memory remap race condition detected for address %p. Waiting 10 ms...", (void *)abortContext->FAR);
		sceKernelDelayThread(10 * 1000);
		threadInFault--;

		return;
	}

	ERROR_LOG(COMMON, "SIGSEGV @ %p invalid access to %p", (void*)ctx.pc, (void *)abortContext->FAR);
	if (nextHandler != nullptr)
		nextHandler(abortContext);
	else
		die("segfault");
}

void os_InstallFaultHandler()
{
	DEBUG_LOG(VMEM, "In the abort handler");
	verify(kuKernelRegisterAbortHandler(abortHandler, &nextHandler, NULL) == 0);
}

void os_UninstallFaultHandler()
{
	kuKernelRegisterAbortHandler(nextHandler, NULL, NULL);
}