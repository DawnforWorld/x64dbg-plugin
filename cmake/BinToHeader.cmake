# BinToHeader.cmake —— 把二进制文件转成 C++ 字节数组头文件（脚本模式）
#
# 用法（cmake -P）：
#   cmake -DINPUT=<bin文件> -DOUTPUT=<头文件> \
#         -DNAMESPACE=<命名空间> -DARRAY=<数组名> -DSIZEVAR=<尺寸变量名> \
#         -P BinToHeader.cmake
#
# 产物格式与 vtdbg 的 gen_iqvw64e_header.py 兼容：
#   namespace <NAMESPACE> {
#   static const uint8_t <ARRAY>[] = { 0x4D, 0x5A, ... };
#   static const size_t <SIZEVAR> = <字节数>;
#   }
#
# 空文件时生成 size=0 的空桩形态（driver[] = {0}），与仓库里提交的
# iqvw64e 空桩同构——加载器对 driver_size==0 有防呆（docs/05 §3）。

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED NAMESPACE
   OR NOT DEFINED ARRAY OR NOT DEFINED SIZEVAR)
  message(FATAL_ERROR "BinToHeader: 需要 INPUT/OUTPUT/NAMESPACE/ARRAY/SIZEVAR 五个参数")
endif()

file(READ "${INPUT}" _hex HEX)
string(REGEX REPLACE "[^0-9A-Fa-f]" "" _hex "${_hex}")
string(LENGTH "${_hex}" _hexlen)
math(EXPR _count "${_hexlen} / 2")

set(_body "")
set(_i 0)
while(_i LESS _count)
  string(SUBSTRING "${_hex}" ${_i} 2 _byte)
  string(APPEND _body "0x${_byte},")
  math(EXPR _i "${_i} + 2")
  if((${_i} % 32) EQUAL 0)
    string(APPEND _body "\n")
  endif()
endwhile()

if(_count EQUAL 0)
  set(_body "0")  # 空桩形态：数组至少有一个元素才是合法 C
endif()

set(_content
"// 本文件由构建自动生成，不要手工修改、不要提交进仓库。
// 源二进制: ${INPUT}
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ${NAMESPACE} {
static const uint8_t ${ARRAY}[] = {
${_body}};
static const size_t ${SIZEVAR} = ${_count};
}  // namespace ${NAMESPACE}
")

file(WRITE "${OUTPUT}" "${_content}")
message(STATUS "BinToHeader: ${INPUT} -> ${OUTPUT} (${_count} bytes)")
