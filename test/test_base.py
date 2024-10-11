import unittest
import subprocess
import os

class BaseTest(unittest.TestCase):

    def setUp(self) -> None:
        """Delete the 'mydb.db' file if it exists before every test."""

        db_file = "mydb.db"
        if os.path.exists(db_file):
            os.remove(db_file)

    # def tearDown(self) -> None:
    #     db_file = "mydb.db"
    #     if os.path.exists(db_file):
    #         os.remove(db_file)

    def run_repl(self, input_data):
        """Runs the REPL binary with the provided input and returns the output."""

        # If input is a tuple, join the elements with newlines
        if isinstance(input_data, tuple):
            input_data = "\n".join(input_data)

        # Encode the string input (whether it's from the tuple or already a string)
        input_data_encoded = input_data.encode()

        process = subprocess.Popen(
            ['./tinysql', 'mydb.db'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        stdout, stderr = process.communicate(input=input_data_encoded)

        # Decode the outputs to strings
        return stdout.decode(), stderr.decode()

    def assert_output(self, input_data, expected_output):
        """Checks if the REPL output matches the expected output."""

        actual_output, _ = self.run_repl(input_data)

        # If expected_output is a tuple, join with newlines
        if isinstance(expected_output, tuple):
            expected_output = "\n".join(expected_output)

        # Strip both actual and expected output to avoid differences due to trailing newlines or spaces
        self.assertEqual(actual_output.strip(), expected_output.strip())

    def assert_error_output(self, input_data, expected_error_output):
        """Checks if the REPL stderr output matches the expected error output."""

        _, actual_error_output = self.run_repl(input_data)

        # If expected_error_output is a tuple, join with newlines
        if isinstance(expected_error_output, tuple):
            expected_error_output = "\n".join(expected_error_output)

        # Strip both actual and expected error output
        self.assertEqual(actual_error_output.strip(), expected_error_output.strip())
