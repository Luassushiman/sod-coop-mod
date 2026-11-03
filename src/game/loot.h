// Lugares de busca (armários, gavetas, carros...): são "nós" do mundo RTS do jogo,
// numerados pelos dados do nível (mesmos números nas duas cópias). Cada lado manda o
// mapa de nós vasculhados; quem recebe marca como vasculhado o que o outro já vasculhou
// (só marca, nunca desmarca). O que sobrou dentro de cada lugar (histórico de busca do
// jogo, últimos 20 lugares) também vai: vale a versão mais nova.
// Thread principal.
#pragma once

namespace sodcoop::loot {

// A cada 0,5 s: lê os nós vasculhados e o que sobrou nos lugares, publica para a rede e
// aplica os do outro lado. host decide empates (o conteúdo do host vale).
void update(bool active, bool host);

}  // namespace sodcoop::loot
