from .test_base import BaseTest

class TestInsert(BaseTest):

    def test_simple_insert(self):
        input_data = (
            "insert 1 user1 person1@example.com",
            "select",
            ".exit\n",
        )

        expected_output = (
            "tinysql > Executed.",
            "tinysql > (1, user1, person1@example.com)",
            "Executed.",
            "tinysql > "
        )

        self.assert_output(input_data, expected_output)

    def test_persistence(self):
        input_data = (
            "insert 1 user1 person1@example.com",
            ".exit\n",
        )

        expected_output = (
            "tinysql > Executed.",
            "tinysql > ",
        )

        self.assert_output(input_data, expected_output)

        input_data2 = (
            "select",
            ".exit\n",
        )

        expected_output2 = (
            "tinysql > (1, user1, person1@example.com)",
            "Executed.",
            "tinysql > ",
        )

        self.assert_output(input_data2, expected_output2)
