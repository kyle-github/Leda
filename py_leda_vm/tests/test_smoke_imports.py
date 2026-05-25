import unittest


class SmokeImportsTest(unittest.TestCase):
    def test_import_packages(self) -> None:
        import compiler  # noqa: F401
        import image  # noqa: F401
        import vm  # noqa: F401
        import runtime  # noqa: F401
        import tools  # noqa: F401

    def test_encode_decode_roundtrip(self) -> None:
        from compiler.encode64 import compose_imm36, decode_word, encode_word

        word = encode_word(opcode=1, a=2, b=3, c=4, d=5, reserved=0)
        decoded = decode_word(word)
        self.assertEqual(decoded.opcode, 1)
        self.assertEqual(decoded.a, 2)
        self.assertEqual(decoded.b, 3)
        self.assertEqual(decoded.c, 4)
        self.assertEqual(decoded.d, 5)
        self.assertEqual(decoded.reserved, 0)
        self.assertEqual(compose_imm36(3, 4, 5), (3 << 24) | (4 << 12) | 5)


if __name__ == "__main__":
    unittest.main()
