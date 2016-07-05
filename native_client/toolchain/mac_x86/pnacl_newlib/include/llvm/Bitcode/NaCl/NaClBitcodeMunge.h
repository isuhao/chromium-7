//===- NaClBitcodeMunge.h - Bitcode Munger ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Test harness for generating a PNaCl bitcode memory buffer from
// an array, and parse/objdump/compress the resulting contents.
//
// Generates a bitcode memory buffer from an array containing 1 or
// more PNaCl records. Used to test errors in PNaCl bitcode.
//
// Bitcode records are modeled using arrays using the format
// specified in NaClBitcodeMungeUtils.h.
//
// Note: Since the header record doesn't have any abbreviation indices
// associated with it, one can use any value. The value will simply be
// ignored.
//
// In addition to specifying the sequence of records, one can also
// define a sequence of edits to be applied to the original sequence
// of records. This allows the same record sequence to be used in
// multiple tests. Again, see NaClBitcodeMungeUtils.h for the
// format of editing arrays.
//
// Generally, you can generate any legal/illegal record
// sequence. However, abbreviations are intimately tied to the
// internals of the bitstream writer and can't contain illegal
// data. Whenever class NaClBitcodeMunger is unable to accept illegal
// data, a corresponding "Fatal" error is generated and execution
// is terminated.
//
// ===---------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H
#define LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/NaCl/NaClBitcodeMungeUtils.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

namespace llvm {

class NaClBitCodeAbbrev;

/// Base class to run tests on munged bitcode files.
class NaClBitcodeMunger {
public:
  /// Creates a bitcode munger, based on the given array of values.
  NaClBitcodeMunger(const uint64_t Records[], size_t RecordsSize,
                    uint64_t RecordTerminator)
      : MungedBitcode(Records, RecordsSize, RecordTerminator),
        RecordTerminator(RecordTerminator),
        DumpResults("Error: No previous dump results!\n"),
        DumpStream(nullptr), FoundErrors(false), RunAsDeathTest(false) {}

  /// Returns true if running as death test.
  bool getRunAsDeathTest() const {
    return RunAsDeathTest;
  }

  /// Sets death test flag. When true, output will be redirected to
  /// the errs() (rather than buffered) so that the test can be
  /// debugged.
  void setRunAsDeathTest(bool NewValue) {
    RunAsDeathTest = NewValue;
  }

  /// Creates MungedInput and DumpStream for running tests, based on
  /// given Munges. Returns true if able to set up test.
  bool setupTest(const uint64_t Munges[], size_t MungesSize, bool AddHeader);

  // TODO(kschimpf): The following function is deprecated and only
  // provided until subzero is updated to use the new API that no
  // longer uses test names.
  bool setupTest(const char *, const uint64_t Munges[], size_t MungesSize,
                 bool AddHeader) {
    return setupTest(Munges, MungesSize, AddHeader);
  }

  /// Cleans up state after a test. Returns true if no errors found.
  bool cleanupTest();

  /// Returns the resulting string generated by the corresponding test.
  const std::string &getTestResults() const {
    return DumpResults;
  }

  /// Returns the lines containing the given Substring, from the
  /// string getTestResults().
  std::string getLinesWithSubstring(const std::string &Substring) const {
    return getLinesWithTextMatch(Substring, false);
  }

  /// Returns the lines starting with the given Prefix, from the string
  /// getTestResults().
  std::string getLinesWithPrefix(const std::string &Prefix) const {
    return getLinesWithTextMatch(Prefix, true);
  }

  /// When NewValue, use error recovery when writing bitcode during
  /// next test.
  void setTryToRecoverOnWrite(bool NewValue) {
    WriteFlags.setTryToRecover(NewValue);
  }

  /// When NewValue, write bad abbreviation index into bitcode when
  /// writing during next test.
  void setWriteBadAbbrevIndex(bool NewValue) {
    WriteFlags.setWriteBadAbbrevIndex(NewValue);
  }

  /// Get access to munged bitcodes.
  NaClMungedBitcode &getMungedBitcode() {
    return MungedBitcode;
  }

  /// Apply given munges to the munged bitcode.
  void munge(const uint64_t Munges[], size_t MungesSize) {
    MungedBitcode.munge(Munges, MungesSize, RecordTerminator);
  }

protected:
  // The bitcode records being munged.
  NaClMungedBitcode MungedBitcode;
  // The value used as record terminator.
  uint64_t RecordTerminator;
  // The results buffer of the last dump.
  std::string DumpResults;
  // The memory buffer containing the munged input.
  std::unique_ptr<MemoryBuffer> MungedInput;
  // The stream containing errors and the objdump of the generated bitcode file.
  raw_ostream *DumpStream;
  // True if any errors were reported.
  bool FoundErrors;
  // The buffer for the contents of the munged input.
  SmallVector<char, 1024> MungedInputBuffer;
  /// The write flags to use when writing bitcode.
  NaClMungedBitcode::WriteFlags WriteFlags;
  // Flag to redirect dump stream if running death test.
  bool RunAsDeathTest;

  // Records that an error occurred, and returns stream to print error
  // message to.
  raw_ostream &Error() {
    FoundErrors = true;
    return getDumpStream() << "error: ";
  }

  // Returns the lines containing the given Substring, from the string
  // getTestResults(). If MustBePrefix, then Substring must match at
  // the beginning of the line.
  std::string getLinesWithTextMatch(const std::string &Substring,
                                    bool MustBePrefix = false) const;

  // Returns the log stream to use. When running death tests, redirect output
  // to the error stream (rather than buffering in DumpStream), so that
  // the output can be seen in gtest death tests.
  raw_ostream &getDumpStream() const {
    return RunAsDeathTest ? errs() : *DumpStream;
  }
};

/// Class to run tests writing munged bitcode.
class NaClWriteMunger : public NaClBitcodeMunger {
public:
  NaClWriteMunger(const uint64_t Records[], size_t RecordsSize,
                  uint64_t RecordTerminator)
      : NaClBitcodeMunger(Records, RecordsSize, RecordTerminator) {}

  /// Writes munged bitcode and puts error messages into DumpResults.
  /// Returns true if successful.
  bool runTest(const uint64_t Munges[], size_t MungesSize);

  /// Same as above, but without any edits.
  bool runTest() {
    uint64_t NoMunges[] = {0};
    return runTest(NoMunges, 0);
  }
};

/// Class to run tests for function llvm::NaClObjDump.
class NaClObjDumpMunger : public NaClBitcodeMunger {
public:

  /// Creates a bitcode munger, based on the given array of values.
  NaClObjDumpMunger(const uint64_t Records[], size_t RecordsSize,
                    uint64_t RecordTerminator)
      : NaClBitcodeMunger(Records, RecordsSize, RecordTerminator) {}

  /// Runs function NaClObjDump on the sequence of records associated
  /// with the instance. The memory buffer containing the bitsequence
  /// associated with the record is automatically generated, and
  /// passed to NaClObjDump. If AddHeader is true, test assumes that the
  /// sequence of records doesn't contain a header record, and the
  /// test should add one. Arguments NoRecords and NoAssembly are
  /// passed to NaClObjDump. Returns true if test succeeds without
  /// errors.
  bool runTestWithFlags(bool AddHeader, bool NoRecords, bool NoAssembly) {
    uint64_t NoMunges[] = {0};
    return runTestWithFlags(NoMunges, 0, AddHeader, NoRecords, NoAssembly);
  }

  /// Same as above except it runs function NaClObjDump with flags
  /// NoRecords and NoAssembly set to false, and AddHeader set to true.
  bool runTest() {
    return runTestWithFlags(true, false, false);
  }

  // TODO(kschimpf): The following function is deprecated and only
  // provided until subzero is updated to use the new API that no
  // longer uses test names.
  bool runTest(const char *) {
    return runTest();
  }

  /// Same as above, but only print out assembly and errors.
  bool runTestForAssembly() {
    return runTestWithFlags(true, true, false);
  }
  /// Same as above, but only generate error messages.
  bool runTestForErrors() {
    return runTestWithFlags(true, true, true);
  }

  /// Runs function llvm::NaClObjDump on the sequence of records
  /// associated with the instance. Array Munges contains the sequence
  /// of edits to apply to the sequence of records when generating the
  /// bitsequence in a memory buffer. This generated bitsequence is
  /// then passed to NaClObjDump.  TestName is the name associated
  /// with the memory buffer.  Arguments NoRecords and NoAssembly are
  /// passed to NaClObjDump. Returns true if test succeeds without
  /// errors.
  bool runTestWithFlags(const uint64_t Munges[], size_t MungesSize,
                        bool AddHeader, bool NoRecords, bool NoAssembly);

  /// Same as above except it runs function NaClObjDump with flags
  /// NoRecords and NoAssembly set to false, and AddHeader set to
  /// true.
  bool runTest(const uint64_t Munges[], size_t MungesSize) {
    return runTestWithFlags(Munges, MungesSize, true, false, false);
  }

  bool runTestForAssembly(const uint64_t Munges[], size_t MungesSize) {
    return runTestWithFlags(Munges, MungesSize, true, true, false);
  }

  // TODO(kschimpf): The following function is deprecated and only
  // provided until subzero is updated to use the new API that no
  // longer uses test names.
  bool runTestForAssembly(const char *, const uint64_t Munges[],
                          size_t MungesSize) {
    return runTestForAssembly(Munges, MungesSize);
  }


  bool runTestForErrors(const uint64_t Munges[], size_t MungesSize) {
    return runTestWithFlags(Munges, MungesSize, true, true, true);
  }
};

// Class to run tests for function NaClParseBitcodeFile.
class NaClParseBitcodeMunger : public NaClBitcodeMunger {
public:
  NaClParseBitcodeMunger(const uint64_t Records[], size_t RecordsSize,
                         uint64_t RecordTerminator)
      : NaClBitcodeMunger(Records, RecordsSize, RecordTerminator) {}

  /// Runs function llvm::NaClParseBitcodeFile, and puts error messages
  /// into DumpResults. Returns true if parse is successful.
  /// TODO(kschimpf) Remove VerboseErrors, no longer useful.
  bool runTest(const uint64_t Munges[], size_t MungesSize,
               bool VerboseErrors = false);

  // Same as above, but without any edits.
  /// TODO(kschimpf) Remove VerboseErrors, no longer useful.
  bool runTest(bool VerboseErrors = false) {
    uint64_t NoMunges[] = {0};
    return runTest(NoMunges, 0, VerboseErrors);
  }
};

// Class to run tests for NaClBitcodeCompressor.compress().
class NaClCompressMunger : public NaClBitcodeMunger {
public:
  NaClCompressMunger(const uint64_t Records[], size_t RecordsSize,
                     uint64_t RecordTerminator)
      : NaClBitcodeMunger(Records, RecordsSize, RecordTerminator) {}

  bool runTest(const uint64_t Munges[], size_t MungesSize);

  bool runTest() {
    uint64_t NoMunges[] = {0};
    return runTest(NoMunges, 0);
  }
};

} // end namespace llvm.

#endif // LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H