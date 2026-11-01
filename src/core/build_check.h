// Verifica que o exe em execução é exatamente o build analisado.
// Todos os endereços do mod valem SOMENTE para este hash.
#pragma once

namespace sodcoop {

// SHA-256 do StateOfDecay.exe (Steam build 845221, FileVersion 15.11.3.5751).
inline constexpr const char* kExpectedExeSha256 =
    "31F4217C3FFAB36FFDEDB03DF8871D95038E44F8C7696A34E5F2D595FE819009";

// Calcula o SHA-256 do exe do processo e compara. Grava o hash no log.
bool isSupportedBuild();

}  // namespace sodcoop
