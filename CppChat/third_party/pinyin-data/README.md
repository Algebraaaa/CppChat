# Contact index data

Source: [mozillazg/pinyin-data](https://github.com/mozillazg/pinyin-data), pinned commit
`9193766130af24d2ac54230be979b2e98ac66223`, MIT (original copyright and full license in LICENSE).
Input `pinyin.txt` SHA256: `621f8ca9eff8519f47e2b17b564fd318161e13bca07eea8c8e04993cd5d3b52e`.

`generate_initials.py <pinyin.txt>` deterministically derives only codepoint/initial pairs.
For multiple pronunciations it uses the upstream preferred first pronunciation; this is not contextual
phrase or surname disambiguation. Unknown characters, digits, punctuation and emoji remain `#`.
The derived table supports supplementary Unicode characters and is embedded through rc.qrc.

Selection: the existing Qt QCollator sorts Chinese names but does not expose alphabetic initials.
ICU Han-Latin is mature but adds a separate deployment dependency to the current Windows Qt Kit.
The maintained pinyin-data dataset is also used by python-pinyin; using only static MIT data avoids
running third-party code or introducing another library/ABI. The bundled parser accepts only the
generated local table; it does not download dictionaries at runtime. Representative simplified,
traditional, supplementary and mixed-script names are covered in tests/qt/client_features_tests.cpp.
The original license is embedded in the application and copied to Windows installation licenses.
