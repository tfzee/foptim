#include "x86_codegen/att_assembler.hpp"

#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCParser/MCAsmParser.h>
#include <llvm/MC/MCParser/MCTargetAsmParser.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>

#include <mutex>

namespace foptim::codegen {

namespace {
std::once_flag init_once;
}

bool assemble_att(std::string_view text, std::vector<u8> &out,
                  std::string &err) {
  std::call_once(init_once, [] {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
  });

  const llvm::Triple triple("x86_64-unknown-linux-gnu");
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (target == nullptr) {
    return false;
  }
  const llvm::MCTargetOptions opts;
  std::unique_ptr<llvm::MCRegisterInfo> mri(target->createMCRegInfo(triple));
  std::unique_ptr<llvm::MCAsmInfo> mai(
      target->createMCAsmInfo(*mri, triple, opts));
  std::unique_ptr<llvm::MCInstrInfo> mii(target->createMCInstrInfo());
  std::unique_ptr<llvm::MCSubtargetInfo> sti(
      target->createMCSubtargetInfo(triple, "x86-64-v3", ""));
  if (!mri || !mai || !mii || !sti) {
    err = "could not create the x86 MC components";
    return false;
  }

  llvm::SourceMgr src_mgr;
  std::string diag;
  llvm::raw_string_ostream diag_os(diag);
  src_mgr.setDiagHandler(
      [](const llvm::SMDiagnostic &d, void *ctx) {
        d.print(nullptr, *static_cast<llvm::raw_string_ostream *>(ctx), false);
      },
      &diag_os);
  src_mgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBufferCopy(
          llvm::StringRef(text.data(), text.size()), "inline asm"),
      llvm::SMLoc());

  llvm::MCContext ctx(triple, mai.get(), mri.get(), sti.get(), &src_mgr, &opts);
  std::unique_ptr<llvm::MCObjectFileInfo> mofi(
      target->createMCObjectFileInfo(ctx, false));
  ctx.setObjectFileInfo(mofi.get());

  llvm::SmallVector<char, 256> buf;
  llvm::raw_svector_ostream os(buf);
  std::unique_ptr<llvm::MCCodeEmitter> emitter(
      target->createMCCodeEmitter(*mii, ctx));
  std::unique_ptr<llvm::MCAsmBackend> backend(
      target->createMCAsmBackend(*sti, *mri, opts));
  std::unique_ptr<llvm::MCObjectWriter> writer(backend->createObjectWriter(os));
  std::unique_ptr<llvm::MCStreamer> streamer(target->createMCObjectStreamer(
      triple, ctx, std::move(backend), std::move(writer), std::move(emitter),
      *sti));
  std::unique_ptr<llvm::MCAsmParser> parser(
      llvm::createMCAsmParser(src_mgr, ctx, *streamer, *mai));
  std::unique_ptr<llvm::MCTargetAsmParser> target_parser(
      target->createMCAsmParser(*sti, *parser, *mii, opts));
  if (!target_parser) {
    err = "could not create the x86 asm parser";
    return false;
  }
  parser->setTargetParser(*target_parser);
  if (parser->Run(false)) {
    diag_os.flush();
    err = diag.empty() ? "assembler failed" : diag;
    return false;
  }
  streamer->finish();

  auto obj = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(llvm::StringRef(buf.data(), buf.size()), "asm"));
  if (!obj) {
    err = llvm::toString(obj.takeError());
    return false;
  }
  // the relocations of .text are in the .rela.text section
  for (const auto &sec : (*obj)->sections()) {
    if (!sec.relocations().empty()) {
      err = "inline asm that needs relocations is not supported";
      return false;
    }
  }
  for (const auto &sec : (*obj)->sections()) {
    auto name = sec.getName();
    if (!name || *name != ".text") {
      continue;
    }
    auto contents = sec.getContents();
    if (!contents) {
      err = llvm::toString(contents.takeError());
      return false;
    }
    out.assign(contents->begin(), contents->end());
    return true;
  }
  err = "assembler produced no text section";
  return false;
}

}  // namespace foptim::codegen
