// Menu em texto para escolher o sobrevivente (F7 abre/fecha; setas escolhem; Enter troca).
// O jogo não guarda o nome dos sobreviventes em texto: cada linha mostra o número na
// lista de membros, o sexo e a roupa (lidos do modelo). Thread principal.
#pragma once

namespace sodcoop::member_menu {

// toggleKey: tecla virtual que abre/fecha (0 = desliga o menu).
// settled/dead: só funciona no mundo e vivo; fecha sozinho se deixar de valer.
void update(int toggleKey, bool settled, bool dead);

}  // namespace sodcoop::member_menu
