#!/bin/bash
# Source this helper after defining FLAGS. Keep the pinned dependency unchanged,
# instrumented, and separate from first-party translation units. Clang's known
# arduinoFFT 2.0.4 absolute-value diagnostic remains visible only on this object.
compile_host_fft() {
  local fft_source="$1" fft_object="$2"
  local compiler="${CXX:-clang++}"
  local compiler_version
  local dependency_flags=()
  compiler_version="$("$compiler" --version)"
  if [[ "$compiler_version" == *clang* ]]; then
    dependency_flags+=(-Wno-error=absolute-value)
  fi
  "$compiler" "${FLAGS[@]}" ${dependency_flags[@]+"${dependency_flags[@]}"} \
    -c "$fft_source/arduinoFFT.cpp" -o "$fft_object"
}
