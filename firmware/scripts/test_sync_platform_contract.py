import hashlib
import json
import unittest

from sync_platform_command_vectors import canonicalize_json, definition_checksum


class CanonicalDefinitionChecksumTest(unittest.TestCase):
    def test_whitespace_and_object_key_order_do_not_change_checksum(self):
        compact = json.loads(
            '{"schemaVersion":"1","typeKey":"fixture","models":[{"z":2,"a":1}]}'
        )
        reordered = json.loads(
            '''
            {
              "models": [ { "a": 1, "z": 2 } ],
              "typeKey": "fixture",
              "schemaVersion": "1"
            }
            '''
        )
        self.assertEqual(canonicalize_json(compact), canonicalize_json(reordered))
        self.assertEqual(definition_checksum(compact), definition_checksum(reordered))
        self.assertEqual(
            hashlib.sha256(
                b'{"models":[{"a":1,"z":2}],"schemaVersion":"1","typeKey":"fixture"}'
            ).hexdigest(),
            definition_checksum(compact),
        )
        self.assertEqual('{"n":100000000000000000000}', canonicalize_json({"n": 1e20}))


if __name__ == "__main__":
    unittest.main()
