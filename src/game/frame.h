// Callback por frame na thread principal do jogo, via hooks em
// CCryAction::PreUpdate e CCryAction::PostUpdate. Todo código que altera o
// jogo deve rodar aqui.
//
// Os callbacks rodam no início do PostUpdate (antes do original): a essa
// altura o jogo já atualizou entidades e animações deste frame, então leituras
// de pose (ex.: GetBonePos) refletem o que vai ser desenhado.
#pragma once

namespace sodcoop::frame {

using Callback = void (*)();

// Registra um callback (antes de installHook). Máximo de 8.
bool addCallback(Callback cb);

// Instala os hooks de PreUpdate e PostUpdate. Exige código já descriptografado.
bool installHook();

// Se true, o PreUpdate sempre recebe haveFocus=true (o jogo não "percebe" a perda de foco).
void setForceFocus(bool force);

}  // namespace sodcoop::frame
