// Leitura do save pelo jogo (NtCreateFile/NtOpenFile do ntdll: toda abertura de arquivo passa por eles).
// Registra no log quando o jogo abre os arquivos do save do Breakdown e, no cliente com
// [net] sync_save=1, segura essa abertura enquanto o save do host está chegando
// (net/save_transfer.h), por no máximo 20 s no total; sem o host conectado, não espera.
// O jogo só abre o save ao clicar em Continuar (medido em 2026-11-04), então a espera só
// acontece se o jogador clicar antes de o save chegar. Depois que o jogo lê, o mod não grava
// mais o save naquela sessão.
#pragma once

namespace sodcoop::save_gate {

// Antes de o jogo ler o save: logo depois da conferência do build.
bool install(bool clientSync);

}  // namespace sodcoop::save_gate
