// Logger em arquivo, thread-safe, com timestamp. Um arquivo por instância do jogo.
#pragma once

namespace sodcoop::log {

// Abre (anexa) o arquivo de log. Seguro chamar mais de uma vez.
void open(const wchar_t* path);
void close();

void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);

}  // namespace sodcoop::log
