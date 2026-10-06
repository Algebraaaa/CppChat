# Telegram-compatible Twemoji static pack

This is the open Twemoji variant supported by Telegram Desktop's emoji generator,
not Telegram's Apple Color Emoji artwork or animated Premium custom emoji.

Artwork: Twitter Twemoji, CC BY 4.0. PNGs copied unchanged from revision `bad3bceeafc901ace42a3dfe0421db6388daafb9`.
https://github.com/twitter/twemoji

Telegram ordering/support list: desktop-app/lib_ui `b9242d3e711d2007e4872d19dd505c3580a63348`, emoji.txt.
https://github.com/desktop-app/lib_ui/blob/b9242d3e711d2007e4872d19dd505c3580a63348/emoji.txt
Telegram generator's Twemoji support:
https://github.com/desktop-app/codegen/blob/master/codegen/emoji/generator.cpp

Names and aliases: iamcal/emoji-data `13ee711e222ea17fe537bfea953c687866f16411`, MIT metadata license.
https://github.com/iamcal/emoji-data
Chinese display names and catalog integration: CppChat.

Packaged: 189 static 72x72 PNGs. Each file's SHA-256 is in catalog.json.
Transport tokens are namespaced as [TG:hex-code], so existing QQ tokens keep their meaning.
Both clients need this catalog to render the new tokens as images.
原 PowerShell 导入脚本已于 2026-10-06 按用户要求删除。上游来源和固定版本信息保留，客户端继续使用已导入的内置资源。
