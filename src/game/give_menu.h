// Menu em texto para dar um item da mochila ao outro jogador (F8 abre/fecha; setas
// escolhem; Enter dá). O item sai da mochila daqui e entra na do outro, com a mesma
// quantidade, durabilidade e munição; se lá não couber, ele volta. Thread principal.
#pragma once

namespace sodcoop::give_menu {

// toggleKey: tecla virtual que abre/fecha (0 = desliga). active: no mundo, vivo e conectado.
// Também recebe os itens que o outro jogador deu.
void update(int toggleKey, bool settled, bool dead, bool connected);

}  // namespace sodcoop::give_menu
