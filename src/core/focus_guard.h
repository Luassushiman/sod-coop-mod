// Impede o jogo de saber que a janela perdeu o foco (ambiente com duas
// instâncias lado a lado). Mesmo com o haveFocus forçado no frame (frame.cpp),
// o jogo abre o menu de pausa ao perder o foco a partir das mensagens do
// Windows. Uma subclasse do WndProc descarta WM_ACTIVATEAPP(FALSE) e
// WM_ACTIVATE(WA_INACTIVE); as demais mensagens seguem normalmente.
#pragma once

namespace sodcoop::focus_guard {

// Inicia uma thread que espera a janela principal existir e instala a subclasse.
void start();

}  // namespace sodcoop::focus_guard
