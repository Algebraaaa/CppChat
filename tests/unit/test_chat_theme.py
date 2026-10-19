"""Static theme contracts; these checks do not replace Windows rendering tests."""

import json
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


CLIENT = Path(__file__).resolve().parents[2] / "CppChat"


def contrast(foreground, background):
    def luminance(color):
        channels = [int(color[index : index + 2], 16) / 255 for index in (1, 3, 5)]
        linear = [
            value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
            for value in channels
        ]
        return sum(weight * value for weight, value in zip((0.2126, 0.7152, 0.0722), linear))

    bright, dark = sorted((luminance(foreground), luminance(background)), reverse=True)
    return (bright + 0.05) / (dark + 0.05)


class ChatThemeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((CLIENT / "style/theme.json").read_text(encoding="utf-8"))
        cls.tokens = json.loads(
            (CLIENT / "style" / cls.manifest["tokens"]).read_text(encoding="utf-8")
        )
        cls.tokens["font.family"] = ", ".join(
            f'"{family}"' for family in cls.manifest["fontFamilies"]
        )
        cls.rules = {}
        for module in cls.manifest["styles"]:
            source = (CLIENT / "style" / module).read_text(encoding="utf-8")
            expanded = re.sub(
                r"\$\{([a-zA-Z][a-zA-Z0-9_.-]*)\}",
                lambda match: cls.tokens[match.group(1)],
                source,
            )
            if "${" in expanded:
                raise ValueError(f"Unexpanded theme expression in {module}")
            expanded = re.sub(r"/\*.*?\*/", "", expanded, flags=re.S)
            for selectors, body in re.findall(r"([^{}]+)\{([^{}]*)\}", expanded):
                declarations = dict(
                    declaration.strip().split(":", 1)
                    for declaration in body.split(";")
                    if declaration.strip()
                )
                for selector in selectors.split(","):
                    cls.rules.setdefault(selector.strip(), {}).update(
                        {key.strip(): value.strip() for key, value in declarations.items()}
                    )

    def test_message_text_and_selection_have_explicit_colors(self):
        rule = self.rules["QTextEdit#chat_message_text"]
        self.assertEqual(rule["color"], self.tokens["color.text.body"])
        self.assertEqual(rule["selection-color"], self.tokens["color.text.primary"])
        self.assertEqual(rule["selection-background-color"], self.tokens["color.selection"])
        self.assertEqual(rule["background"], "transparent")

    def test_both_bubble_roles_use_theme_backgrounds(self):
        for role in ("self", "other"):
            with self.subTest(role=role):
                rule = self.rules[f'BubbleFrame[chatRole="{role}"]']
                self.assertEqual(
                    rule["qproperty-backgroundColor"], self.tokens[f"color.chat.bubble.{role}"]
                )
        header = (CLIENT / "widgets/bubbleframe.h").read_text(encoding="utf-8")
        source = (CLIENT / "widgets/bubbleframe.cpp").read_text(encoding="utf-8")
        self.assertIn(
            "Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)",
            header,
        )
        self.assertIn('setProperty("chatRole"', source)
        painter = source.split("void BubbleFrame::paintEvent", 1)[1]
        self.assertIn("painter.setBrush(QBrush(m_backgroundColor))", painter)
        self.assertNotIn("bk_color", painter)

    def test_normal_and_selected_message_contrast(self):
        for role in ("self", "other"):
            with self.subTest(role=role):
                self.assertGreaterEqual(
                    contrast(
                        self.tokens["color.text.body"], self.tokens[f"color.chat.bubble.{role}"]
                    ),
                    4.5,
                )
        self.assertGreaterEqual(
            contrast(self.tokens["color.text.primary"], self.tokens["color.selection"]), 4.5
        )

    def test_theme_styles_are_embedded_in_the_client(self):
        resources = {}
        for group in ET.parse(CLIENT / "rc.qrc").getroot().findall("qresource"):
            prefix = group.get("prefix", "").rstrip("/")
            for entry in group.findall("file"):
                resources[":" + prefix + "/" + entry.get("alias", entry.text)] = entry.text
        for relative in [
            "theme.json",
            self.manifest["tokens"],
            self.manifest["darkTokens"],
            *self.manifest["styles"],
        ]:
            with self.subTest(resource=relative):
                self.assertIn(":/style/" + relative, resources)
                self.assertTrue((CLIENT / resources[":/style/" + relative]).is_file())
        self.assertEqual(len(self.manifest["styles"]), len(set(self.manifest["styles"])))

    def test_dark_tokens_cover_readable_surfaces_and_selections(self):
        overrides = json.loads(
            (CLIENT / "style" / self.manifest["darkTokens"]).read_text(encoding="utf-8")
        )
        self.assertTrue(all(key.startswith("color.") and key in self.tokens for key in overrides))
        dark = self.tokens | overrides
        for background in (
            "color.surface",
            "color.surface.page",
            "color.surface.subtle",
            "color.chat.bubble.self",
            "color.chat.bubble.other",
        ):
            with self.subTest(background=background):
                self.assertGreaterEqual(contrast(dark["color.text.body"], dark[background]), 4.5)
        for foreground, background in (
            ("color.text.primary", "color.selection"),
            ("color.text.onBrand", "color.brand"),
            ("color.text.onSelected", "color.list.selected"),
            ("color.text.muted", "color.surface"),
            ("color.link", "color.surface"),
        ):
            with self.subTest(foreground=foreground, background=background):
                self.assertGreaterEqual(contrast(dark[foreground], dark[background]), 4.5)

    def test_normal_vector_ink_is_visible_in_both_schemes(self):
        icon = ET.parse(CLIENT / "Resources/icons/conversation-bell-off.svg").getroot()
        ink = icon.attrib["stroke"]
        dark = json.loads(
            (CLIENT / "style" / self.manifest["darkTokens"]).read_text(encoding="utf-8")
        )
        for background in (
            self.tokens["color.surface"],
            dark["color.surface"],
            dark["color.panel"],
        ):
            self.assertGreaterEqual(contrast(ink, background), 3)
        for name in ("minimize", "maximize", "restore", "close"):
            window = ET.parse(CLIENT / f"Resources/window/{name}.svg").getroot()
            for shape in window:
                self.assertGreaterEqual(
                    contrast(shape.attrib["stroke"], dark["color.surface.subtle"]), 3
                )


if __name__ == "__main__":
    unittest.main()
