# Zamienia plik (np. listę CA w PEM) na tablicę C zakończoną \0.
# Użycie: cmake -DIN=plik -DOUT=wynik.c -DNAME=symbol -P embed.cmake
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
file(WRITE "${OUT}"
  "// Wygenerowane z ${IN}, nie edytuj.\n"
  "const unsigned char ${NAME}[] = {${arr}0x00};\n"
  "const unsigned int ${NAME}_len = sizeof(${NAME});\n")
