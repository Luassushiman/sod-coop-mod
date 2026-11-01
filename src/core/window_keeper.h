// Mantém a janela do jogo numa posição/tamanho fixos (ambiente de teste com
// duas instâncias lado a lado). O jogo redimensiona a própria janela ao abrir
// e ao carregar; uma thread confere a cada segundo e corrige quando o tamanho
// muda. Se o usuário só arrastar a janela, ela fica onde ele deixou.
#pragma once

namespace sodcoop::window_keeper {

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    bool enabled() const { return width > 0 && height > 0; }
};

// Inicia a thread de monitoramento (sem efeito se rect desabilitado).
void start(const Rect& rect);

}  // namespace sodcoop::window_keeper
