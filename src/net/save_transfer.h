// Save do Breakdown enviado do host para o cliente pela rede (sem copiar arquivos à mão).
// Arquivos: Sandbox*.ulsave e UserStats-dlc.cfg da pasta <jogo>\USER\<SteamID64>\.
// Pacote: "SODS", u32 quantidade, e para cada arquivo: u16 tamanho do nome, nome (ASCII),
// u32 tamanho, bytes. Os nomes vão em ordem alfabética, então o mesmo save dá o mesmo pacote.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sodcoop::net::save_transfer {

// Pasta do save desta conta (com barra final); vazia se não achar a conta.
std::wstring saveDirectory();

// Monta o pacote com o save local. false se não há save do Breakdown.
bool buildBundle(std::vector<uint8_t>& out, int& files);

uint32_t crc32(const uint8_t* data, size_t size);

// true enquanto esta thread lê ou grava o save pelo próprio mod (game/save_gate não segura).
bool modIoOnThisThread();

// Cliente: faz backup do save atual em <jogo>\sodcoop_save_backups\<data_hora>\ e grava o
// pacote no lugar (os Sandbox*.ulsave que o host não tem são apagados, depois do backup).
bool applyBundle(const std::vector<uint8_t>& bundle, std::string& error);

}  // namespace sodcoop::net::save_transfer
