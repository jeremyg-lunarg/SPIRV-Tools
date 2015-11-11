//
// Copyright (c) 2015 The Khronos Group Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and/or associated documentation files (the
// "Materials"), to deal in the Materials without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Materials, and to
// permit persons to whom the Materials are furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Materials.
//
// MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
// KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
// SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
//    https://www.khronos.org/registry/
//
// THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

#include "UnitSPIRV.h"

#include <sstream>

#include "TestFixture.h"
#include "gmock/gmock.h"

using ::testing::Eq;
using spvtest::AutoText;

namespace {

class BinaryToText : public ::testing::Test {
 public:
  virtual void SetUp() {
    const char* textStr = R"(
      OpSource OpenCL 12
      OpMemoryModel Physical64 OpenCL
      OpSourceExtension "PlaceholderExtensionName"
      OpEntryPoint Kernel %1 "foo"
      OpExecutionMode %1 LocalSizeHint 1 1 1
 %2 = OpTypeVoid
 %3 = OpTypeBool
 %4 = OpTypeInt 8 0
 %5 = OpTypeInt 8 1
 %6 = OpTypeInt 16 0
 %7 = OpTypeInt 16 1
 %8 = OpTypeInt 32 0
 %9 = OpTypeInt 32 1
%10 = OpTypeInt 64 0
%11 = OpTypeInt 64 1
%12 = OpTypeFloat 16
%13 = OpTypeFloat 32
%14 = OpTypeFloat 64
%15 = OpTypeVector %4 2
)";
    spv_text_t text = {textStr, strlen(textStr)};
    spv_diagnostic diagnostic = nullptr;
    spv_result_t error =
        spvTextToBinary(text.str, text.length, &binary, &diagnostic);
    if (error) {
      spvDiagnosticPrint(diagnostic);
      spvDiagnosticDestroy(diagnostic);
      ASSERT_EQ(SPV_SUCCESS, error);
    }
  }

  virtual void TearDown() { spvBinaryDestroy(binary); }

  // Compiles the given assembly text, and saves it into 'binary'.
  void CompileSuccessfully(std::string text) {
    spv_diagnostic diagnostic = nullptr;
    EXPECT_EQ(SPV_SUCCESS,
              spvTextToBinary(text.c_str(), text.size(), &binary, &diagnostic));
  }

  spv_binary binary;
};

TEST_F(BinaryToText, Default) {
  spv_text text = nullptr;
  spv_diagnostic diagnostic = nullptr;
  ASSERT_EQ(SPV_SUCCESS, spvBinaryToText(binary->code, binary->wordCount,
                                         SPV_BINARY_TO_TEXT_OPTION_NONE, &text,
                                         &diagnostic));
  printf("%s", text->str);
  spvTextDestroy(text);
}

TEST_F(BinaryToText, MissingModule) {
  spv_text text;
  spv_diagnostic diagnostic = nullptr;
  EXPECT_EQ(SPV_ERROR_INVALID_BINARY,
            spvBinaryToText(nullptr, 42, SPV_BINARY_TO_TEXT_OPTION_NONE, &text,
                            &diagnostic));
  EXPECT_THAT(diagnostic->error, Eq(std::string("Missing module.")));
  if (diagnostic) {
    spvDiagnosticPrint(diagnostic);
    spvDiagnosticDestroy(diagnostic);
  }
}

TEST_F(BinaryToText, TruncatedModule) {
  // Make a valid module with zero instructions.
  CompileSuccessfully("");
  EXPECT_EQ(SPV_INDEX_INSTRUCTION, binary->wordCount);

  for (int length = 0; length < SPV_INDEX_INSTRUCTION; length++) {
    spv_text text = nullptr;
    spv_diagnostic diagnostic = nullptr;
    EXPECT_EQ(
        SPV_ERROR_INVALID_BINARY,
        spvBinaryToText(binary->code, length, SPV_BINARY_TO_TEXT_OPTION_NONE,
                        &text, &diagnostic));
    ASSERT_NE(nullptr, diagnostic);
    std::stringstream expected;
    expected << "Module has incomplete header: only " << length
             << " words instead of " << SPV_INDEX_INSTRUCTION;
    EXPECT_THAT(diagnostic->error, Eq(expected.str()));
    spvDiagnosticDestroy(diagnostic);
  }
}

TEST_F(BinaryToText, InvalidMagicNumber) {
  CompileSuccessfully("");
  std::vector<uint32_t> damaged_binary(binary->code,
                                       binary->code + binary->wordCount);
  damaged_binary[SPV_INDEX_MAGIC_NUMBER] ^= 123;

  spv_diagnostic diagnostic = nullptr;
  spv_text text;
  EXPECT_EQ(
      SPV_ERROR_INVALID_BINARY,
      spvBinaryToText(damaged_binary.data(), damaged_binary.size(),
                      SPV_BINARY_TO_TEXT_OPTION_NONE, &text, &diagnostic));
  ASSERT_NE(nullptr, diagnostic);
  std::stringstream expected;
  expected << "Invalid SPIR-V magic number '" << std::hex
           << damaged_binary[SPV_INDEX_MAGIC_NUMBER] << "'.";
  EXPECT_THAT(diagnostic->error, Eq(expected.str()));
  spvDiagnosticDestroy(diagnostic);
}

TEST_F(BinaryToText, InvalidDiagnostic) {
  spv_text text;
  ASSERT_EQ(SPV_ERROR_INVALID_DIAGNOSTIC,
            spvBinaryToText(binary->code, binary->wordCount,
                            SPV_BINARY_TO_TEXT_OPTION_NONE, &text, nullptr));
}

struct FailedDecodeCase {
  std::string source_text;
  std::vector<uint32_t> appended_instruction;
  std::string expected_error_message;
};

using BinaryToTextFail =
    spvtest::TextToBinaryTestBase<::testing::TestWithParam<FailedDecodeCase>>;

TEST_P(BinaryToTextFail, EncodeSuccessfullyDecodeFailed) {
  EXPECT_THAT(EncodeSuccessfullyDecodeFailed(GetParam().source_text,
                                             GetParam().appended_instruction),
              Eq(GetParam().expected_error_message));
}

INSTANTIATE_TEST_CASE_P(
    InvalidIds, BinaryToTextFail,
    ::testing::ValuesIn(std::vector<FailedDecodeCase>{
        {"", spvtest::MakeInstruction(SpvOpTypeVoid, {0}),
         "Error: Result Id is 0"},
        {"", spvtest::MakeInstruction(SpvOpConstant, {0, 1, 42}),
         "Error: Type Id is 0"},
        {"%1 = OpTypeVoid", spvtest::MakeInstruction(SpvOpTypeVoid, {1}),
         "Id 1 is defined more than once"},
        {"%1 = OpTypeVoid\n"
         "%2 = OpNot %1 %foo",
         spvtest::MakeInstruction(SpvOpNot, {1, 2, 3}),
         "Id 2 is defined more than once"},
        {"%1 = OpTypeVoid\n"
         "%2 = OpNot %1 %foo",
         spvtest::MakeInstruction(SpvOpNot, {1, 1, 3}),
         "Id 1 is defined more than once"},
        // The following are the two failure cases for
        // Parser::setNumericTypeInfoForType.
        {"", spvtest::MakeInstruction(SpvOpConstant, {500, 1, 42}),
         "Type Id 500 is not a type"},
        {"%1 = OpTypeInt 32 0\n"
         "%2 = OpTypeVector %1 4",
         spvtest::MakeInstruction(SpvOpConstant, {2, 3, 999}),
         "Type Id 2 is not a scalar numeric type"},
    }));

INSTANTIATE_TEST_CASE_P(
    InvalidIdsCheckedDuringLiteralCaseParsing, BinaryToTextFail,
    ::testing::ValuesIn(std::vector<FailedDecodeCase>{
        {"", spvtest::MakeInstruction(SpvOpSwitch, {1, 2, 3, 4}),
         "Invalid OpSwitch: selector id 1 has no type"},
        {"%1 = OpTypeVoid\n",
         spvtest::MakeInstruction(SpvOpSwitch, {1, 2, 3, 4}),
         "Invalid OpSwitch: selector id 1 is a type, not a value"},
        {"%1 = OpConstantTrue !500",
         spvtest::MakeInstruction(SpvOpSwitch, {1, 2, 3, 4}),
         "Type Id 500 is not a type"},
        {"%1 = OpTypeFloat 32\n%2 = OpConstant %1 1.5",
         spvtest::MakeInstruction(SpvOpSwitch, {2, 3, 4, 5}),
         "Invalid OpSwitch: selector id 2 is not a scalar integer"},
    }));

TEST(BinaryToTextSmall, OneInstruction) {
  // TODO(dneto): This test could/should be refactored.
  spv_binary binary;
  spv_diagnostic diagnostic = nullptr;
  const char* input = "OpSource OpenCL 12";
  spv_result_t error =
      spvTextToBinary(input, strlen(input), &binary, &diagnostic);
  ASSERT_EQ(SPV_SUCCESS, error);
  spv_text text = nullptr;
  error = spvBinaryToText(binary->code, binary->wordCount,
                          SPV_BINARY_TO_TEXT_OPTION_NONE, &text, &diagnostic);
  EXPECT_EQ(SPV_SUCCESS, error);
  if (error) {
    spvDiagnosticPrint(diagnostic);
    spvDiagnosticDestroy(diagnostic);
  }
  spvTextDestroy(text);
}

// Exercise the case where an operand itself has operands.
// This could detect problems in updating the expected-set-of-operands
// list.
TEST(BinaryToTextSmall, OperandWithOperands) {
  spv_binary binary;
  spv_diagnostic diagnostic = nullptr;

  AutoText input(R"(OpEntryPoint Kernel %fn "foo"
                 OpExecutionMode %fn LocalSizeHint 100 200 300
                 %void = OpTypeVoid
                 %fnType = OpTypeFunction %void
                 %fn = OpFunction %void None %fnType
                 )");
  spv_result_t error = spvTextToBinary(input.str.c_str(), input.str.length(),
                                       &binary, &diagnostic);
  ASSERT_EQ(SPV_SUCCESS, error);
  spv_text text = nullptr;
  error = spvBinaryToText(binary->code, binary->wordCount,
                          SPV_BINARY_TO_TEXT_OPTION_NONE, &text, &diagnostic);
  EXPECT_EQ(SPV_SUCCESS, error);
  if (error) {
    spvDiagnosticPrint(diagnostic);
    spvDiagnosticDestroy(diagnostic);
  }
  spvTextDestroy(text);
}

TEST(BinaryToTextSmall, LiteralInt64) {
  spv_binary binary;
  spv_diagnostic diagnostic = nullptr;

  AutoText input("%1 = OpTypeInt 64 0\n%2 = OpConstant %1 123456789021\n");
  spv_result_t error = spvTextToBinary(input.str.c_str(), input.str.length(),
                                       &binary, &diagnostic);
  ASSERT_EQ(SPV_SUCCESS, error);
  spv_text text = nullptr;
  error = spvBinaryToText(binary->code, binary->wordCount,
                          SPV_BINARY_TO_TEXT_OPTION_NONE, &text, &diagnostic);
  if (error) {
    spvDiagnosticPrint(diagnostic);
    spvDiagnosticDestroy(diagnostic);
  }
  ASSERT_EQ(SPV_SUCCESS, error);
  const std::string header =
      "; SPIR-V\n; Version: 99\n; Generator: Khronos\n; "
      "Bound: 3\n; Schema: 0\n";
  EXPECT_EQ(header + input.str, text->str);
  spvTextDestroy(text);
}

TEST(BinaryToTextSmall, LiteralDouble) {
  spv_binary binary;
  spv_diagnostic diagnostic = nullptr;

  AutoText input(
      "%1 = OpTypeFloat 64\n%2 = OpSpecConstant %1 3.1415926535897930");
  spv_result_t error = spvTextToBinary(input.str.c_str(), input.str.length(),
                                       &binary, &diagnostic);
  ASSERT_EQ(SPV_SUCCESS, error);
  spv_text text = nullptr;
  error = spvBinaryToText(binary->code, binary->wordCount,
                          SPV_BINARY_TO_TEXT_OPTION_NONE, &text, &diagnostic);
  if (error) {
    spvDiagnosticPrint(diagnostic);
    spvDiagnosticDestroy(diagnostic);
  }
  ASSERT_EQ(SPV_SUCCESS, error);
  const std::string output =
      R"(; SPIR-V
; Version: 99
; Generator: Khronos
; Bound: 3
; Schema: 0
%1 = OpTypeFloat 64
%2 = OpSpecConstant %1 3.14159265358979
)";
  EXPECT_EQ(output, text->str) << text->str;
  spvTextDestroy(text);
}

using RoundTripInstructionsTest =
    spvtest::TextToBinaryTestBase<::testing::TestWithParam<std::string>>;

TEST_P(RoundTripInstructionsTest, Sample) {
  EXPECT_THAT(EncodeAndDecodeSuccessfully(GetParam()), Eq(GetParam()));
};

// clang-format off
INSTANTIATE_TEST_CASE_P(
    MemoryAccessMasks, RoundTripInstructionsTest,
    ::testing::ValuesIn(std::vector<std::string>{
        "OpStore %1 %2\n",       // 3 words long.
        "OpStore %1 %2 None\n",  // 4 words long, explicit final 0.
        "OpStore %1 %2 Volatile\n",
        "OpStore %1 %2 Aligned 8\n",
        "OpStore %1 %2 Nontemporal\n",
        // Combinations show the names from LSB to MSB
        "OpStore %1 %2 Volatile|Aligned 16\n",
        "OpStore %1 %2 Volatile|Nontemporal\n",
        "OpStore %1 %2 Volatile|Aligned|Nontemporal 32\n",
    }));
// clang-format on

INSTANTIATE_TEST_CASE_P(
    FPFastMathModeMasks, RoundTripInstructionsTest,
    ::testing::ValuesIn(std::vector<std::string>{
        "OpDecorate %1 FPFastMathMode None\n",
        "OpDecorate %1 FPFastMathMode NotNaN\n",
        "OpDecorate %1 FPFastMathMode NotInf\n",
        "OpDecorate %1 FPFastMathMode NSZ\n",
        "OpDecorate %1 FPFastMathMode AllowRecip\n",
        "OpDecorate %1 FPFastMathMode Fast\n",
        // Combinations show the names from LSB to MSB
        "OpDecorate %1 FPFastMathMode NotNaN|NotInf\n",
        "OpDecorate %1 FPFastMathMode NSZ|AllowRecip\n",
        "OpDecorate %1 FPFastMathMode NotNaN|NotInf|NSZ|AllowRecip|Fast\n",
    }));

INSTANTIATE_TEST_CASE_P(LoopControlMasks, RoundTripInstructionsTest,
                        ::testing::ValuesIn(std::vector<std::string>{
                            "OpLoopMerge %1 %2 None\n",
                            "OpLoopMerge %1 %2 Unroll\n",
                            "OpLoopMerge %1 %2 DontUnroll\n",
                            "OpLoopMerge %1 %2 Unroll|DontUnroll\n",
                        }));

INSTANTIATE_TEST_CASE_P(SelectionControlMasks, RoundTripInstructionsTest,
                        ::testing::ValuesIn(std::vector<std::string>{
                            "OpSelectionMerge %1 None\n",
                            "OpSelectionMerge %1 Flatten\n",
                            "OpSelectionMerge %1 DontFlatten\n",
                            "OpSelectionMerge %1 Flatten|DontFlatten\n",
                        }));

// clang-format off
INSTANTIATE_TEST_CASE_P(
    FunctionControlMasks, RoundTripInstructionsTest,
    ::testing::ValuesIn(std::vector<std::string>{
        "%2 = OpFunction %1 None %3\n",
        "%2 = OpFunction %1 Inline %3\n",
        "%2 = OpFunction %1 DontInline %3\n",
        "%2 = OpFunction %1 Pure %3\n",
        "%2 = OpFunction %1 Const %3\n",
        "%2 = OpFunction %1 Inline|Pure|Const %3\n",
        "%2 = OpFunction %1 DontInline|Const %3\n",
    }));
// clang-format on

// clang-format off
INSTANTIATE_TEST_CASE_P(
    ImageMasks, RoundTripInstructionsTest,
    ::testing::ValuesIn(std::vector<std::string>{
        "%2 = OpImageFetch %1 %3 %4\n",
        "%2 = OpImageFetch %1 %3 %4 None\n",
        "%2 = OpImageFetch %1 %3 %4 Bias %5\n",
        "%2 = OpImageFetch %1 %3 %4 Lod %5\n",
        "%2 = OpImageFetch %1 %3 %4 Grad %5 %6\n",
        "%2 = OpImageFetch %1 %3 %4 ConstOffset %5\n",
        "%2 = OpImageFetch %1 %3 %4 Offset %5\n",
        "%2 = OpImageFetch %1 %3 %4 ConstOffsets %5\n",
        "%2 = OpImageFetch %1 %3 %4 Sample %5\n",
        "%2 = OpImageFetch %1 %3 %4 MinLod %5\n",
        "%2 = OpImageFetch %1 %3 %4 Bias|Lod|Grad %5 %6 %7 %8\n",
        "%2 = OpImageFetch %1 %3 %4 ConstOffset|Offset|ConstOffsets"
              " %5 %6 %7\n",
        "%2 = OpImageFetch %1 %3 %4 Sample|MinLod %5 %6\n",
        "%2 = OpImageFetch %1 %3 %4"
              " Bias|Lod|Grad|ConstOffset|Offset|ConstOffsets|Sample|MinLod"
              " %5 %6 %7 %8 %9 %10 %11 %12 %13\n"}));
// clang-format on

using MaskSorting = spvtest::TextToBinaryTest;

TEST_F(MaskSorting, MasksAreSortedFromLSBToMSB) {
  EXPECT_THAT(EncodeAndDecodeSuccessfully(
                  "OpStore %1 %2 Nontemporal|Aligned|Volatile 32"),
              Eq("OpStore %1 %2 Volatile|Aligned|Nontemporal 32\n"));
  EXPECT_THAT(
      EncodeAndDecodeSuccessfully(
          "OpDecorate %1 FPFastMathMode NotInf|Fast|AllowRecip|NotNaN|NSZ"),
      Eq("OpDecorate %1 FPFastMathMode NotNaN|NotInf|NSZ|AllowRecip|Fast\n"));
  EXPECT_THAT(
      EncodeAndDecodeSuccessfully("OpLoopMerge %1 %2 DontUnroll|Unroll"),
      Eq("OpLoopMerge %1 %2 Unroll|DontUnroll\n"));
  EXPECT_THAT(
      EncodeAndDecodeSuccessfully("OpSelectionMerge %1 DontFlatten|Flatten"),
      Eq("OpSelectionMerge %1 Flatten|DontFlatten\n"));
  EXPECT_THAT(EncodeAndDecodeSuccessfully(
                  "%2 = OpFunction %1 DontInline|Const|Pure|Inline %3"),
              Eq("%2 = OpFunction %1 Inline|DontInline|Pure|Const %3\n"));
  EXPECT_THAT(EncodeAndDecodeSuccessfully(
                  "%2 = OpImageFetch %1 %3 %4"
                  " MinLod|Sample|Offset|Lod|Grad|ConstOffsets|ConstOffset|Bias"
                  " %5 %6 %7 %8 %9 %10 %11 %12 %13\n"),
              Eq("%2 = OpImageFetch %1 %3 %4"
                 " Bias|Lod|Grad|ConstOffset|Offset|ConstOffsets|Sample|MinLod"
                 " %5 %6 %7 %8 %9 %10 %11 %12 %13\n"));
}

using OperandTypeTest = spvtest::TextToBinaryTest;

TEST_F(OperandTypeTest, OptionalTypedLiteralNumber) {
  const std::string input =
      "%1 = OpTypeInt 32 0\n"
      "%2 = OpConstant %1 42\n"
      "OpSwitch %2 %3 100 %4\n";
  EXPECT_EQ(input, EncodeAndDecodeSuccessfully(input));
}

}  // anonymous namespace
