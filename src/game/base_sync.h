// Base da comunidade: o armário de suprimentos (inventário de 256 vagas) e os cinco recursos
// do estoque (munição, comida, materiais, remédios, combustível) iguais nas duas cópias.
// Cada lado manda o que mudou; o host manda tudo a cada 5 s (corrige pacotes perdidos).
// Só com base: sem ela o armário não existe. Ver docs/RE_ENDERECOS.md, 4h. Thread principal.
#pragma once

namespace sodcoop::base_sync {

// active: no mundo e conectado. host: o host é quem manda o estado completo.
void update(bool active, bool host);

// Teste: termina na hora a construção da vaga (cancela a ação, devolve os recursos como
// estavam e monta a variante pronta). A outra cópia recebe pela sincronização.
// variant >= 0: monta essa variante (índice na definição) em vez da pronta padrão, e vale
// também sem ação em andamento (teste de melhoria).
void finishFacility(int socket, int variant = -1);

}  // namespace sodcoop::base_sync
