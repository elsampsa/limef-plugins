#!/usr/bin/env python3
"""A single-file implementation of a mini test-framework:

- YAML configuration for test organization
- Parallel test execution
- Valgrind support with error checking
- Fixture handling (creation and comparison)
- Detailed error reporting for fixture mismatches
- Full output dump to file
- Proper exit codes and summaries
"""
import argparse
import os
import sys
import yaml
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple, NamedTuple
import subprocess
import re
import concurrent.futures
from pathlib import Path
from string import Template

TIMEOUT=40 # hardcoded timeout for all tests

@dataclass
class TestConfig:
    """Configuration for a single test, including both test parameters and identification"""
    # Test identification
    group: str
    binary: str
    test_id: str

    # Test configuration
    valgrind: bool = False
    valgrind_args: Optional[List[str]] = None
    valgrind_suppressions: Optional[str] = None
    fixtures: bool = False
    verbosity: int = 0
    about: str = ""
    fixed: bool = False
    python: bool = False
    timeout: int = 20 # default 20 sec timeout for every test
    env: dict = None

    # Output assertions
    expect_stdout: Optional[List[str]] = None   # strings that MUST appear in stdout
    reject_stdout: Optional[List[str]] = None   # strings that must NOT appear in stdout
    expect_stderr: Optional[List[str]] = None   # strings that MUST appear in stderr
    reject_stderr: Optional[List[str]] = None   # strings that must NOT appear in stderr

    num: int = 0

@dataclass
class TestFrameworkConfig:
    yaml_file: str
    groups: List[str]
    tests: List[str]
    use_fixtures: bool
    dump: bool          # produce/overwrite .out dump files
    override: bool
    use_valgrind: bool
    fixture_dir: str    # input media files
    dump_dir: str       # output .out dump files
    bin_dir: str
    lib_dir: str
    env_vars: Dict[str, str]
    max_workers: int = 4
    tests_config: Dict[str, Dict[str, Dict[str, TestConfig]]] = None
    full_output: Optional[str] = None
    verbose: bool = False
    py_dir: Optional[str] = None

class TestResult(NamedTuple):
    command: str
    exit_code: int
    stdout: str
    stderr: str
    valgrind_error: Optional[str] = None
    fixture_error: Optional[str] = None
    timeout: bool = False
    assertion_error: Optional[str] = None

def parse_cli_args() -> TestFrameworkConfig:
    """Parse command line arguments for the test framework."""
    parser = argparse.ArgumentParser(description='Test Framework Runner')
    parser.add_argument('--yaml', required=True, help='Path to YAML config file')
    parser.add_argument('--groups', required=True, help='Comma-separated list of test groups to run')
    parser.add_argument('--tests', required=False, default=None, help='Comma-separated list of tests to run, i.e.: frame_test:1,frame_test:2, etc.')
    parser.add_argument('--fixtures', action='store_true', help='Compare produced output to saved .out dumps')
    parser.add_argument('--dump', action='store_true', help='Produce/overwrite .out dump files - for tests with fixed:false')
    parser.add_argument('--override', action="store_true", help='if used with --dump, overwrites dumps also for tests with fixed:true')
    parser.add_argument('--verbose', action="store_true", help='more verbosity about the test commands etc', default=False)
    parser.add_argument('--valgrind', action='store_true', help='Run with valgrind when available')
    parser.add_argument('--fixture-dir', required=True, help='Directory containing input media files (fixtures)')
    parser.add_argument('--dump-dir', required=True, help='Directory for .out dump files (test output snapshots)')
    parser.add_argument('--bin-dir', required=True, help='Directory containing test binaries')
    parser.add_argument('--lib-dir', required=True, help='Directory containing shared libraries (for LD_LIBRARY_PATH)')
    parser.add_argument('--env', nargs='*', help='Environment variables to override (KEY=VALUE format)')
    parser.add_argument('--jobs', type=int, default=4, help='Maximum number of parallel jobs')
    parser.add_argument('--full-output', help='Path to file for complete test output', required=False, default=None)
    parser.add_argument('--py-dir', required=False, default=None, help='Directory containing Python test scripts')

    args = parser.parse_args()

    env_vars = {}
    if args.env:
        for env_arg in args.env:
            if '=' in env_arg:
                key, value = env_arg.split('=', 1)
                env_vars[key] = value

    return TestFrameworkConfig(
        yaml_file=args.yaml,
        groups=args.groups.split(','),
        tests=args.tests,
        use_fixtures=args.fixtures,
        dump=args.dump,
        override=args.override,
        use_valgrind=args.valgrind,
        fixture_dir=args.fixture_dir,
        dump_dir=args.dump_dir,
        bin_dir=args.bin_dir,
        lib_dir=args.lib_dir,
        env_vars=env_vars,
        max_workers=args.jobs,
        full_output=args.full_output,
        verbose=args.verbose,
        py_dir=args.py_dir
    )

def load_yaml_config(config: TestFrameworkConfig) -> TestFrameworkConfig:
    """Load and parse the YAML configuration file."""
    try:
        with open(config.yaml_file) as f:
            template = Template(f.read())
            yaml_data = yaml.safe_load(template.substitute(
                fixture_dir=config.fixture_dir,
                dump_dir=config.dump_dir,
                lib_dir=config.lib_dir
            ))

        env_vars = yaml_data.get('env', {})
        env_vars.update(config.env_vars)
        config.env_vars = env_vars

        target_tests = []
        if config.tests is not None:
            tests = config.tests.split(",")
            for test in tests:
                try:
                    binary_name, test_id = test.split(":")
                    test_id = int(test_id)
                except Exception as e:
                    print("\nERROR: for --tests use format test_binary:N,test_binary:N,etc\n")
                    sys.exit(2)
                target_tests.append((binary_name, test_id))

        tests_config = {}
        for group_name, group_data in yaml_data.get('groups', {}).items():
            if group_name not in config.groups:
                continue

            # Extract group-level env (if any)
            group_env = group_data.get('env', {})

            tests_config[group_name] = {}
            for binary_name, test_cases in group_data.items():
                if binary_name == 'env':  # Skip the env key, it's not a test binary
                    continue
                tests_config[group_name][binary_name] = {}
                for test_id, test_data in test_cases.items():
                    if (len(target_tests) < 1) or ((binary_name, test_id) in target_tests):
                        valgrind_args = None
                        if 'valgrind-args' in test_data:
                            valgrind_args = test_data['valgrind-args'].split()

                        # Merge group env with test env (test overrides group)
                        test_env = {**group_env, **test_data.get('env', {})}

                        # Parse output assertions (accept string or list)
                        def _as_list(val):
                            if val is None: return None
                            return [val] if isinstance(val, str) else val

                        tests_config[group_name][binary_name][test_id] = TestConfig(
                            group=group_name,
                            binary=binary_name,
                            test_id=str(test_id),
                            valgrind=test_data.get('valgrind', False),
                            valgrind_args=valgrind_args,
                            valgrind_suppressions=test_data.get('valgrind-suppressions'),
                            fixtures=test_data.get('fixtures', False),
                            verbosity=test_data.get('verbosity', 0),
                            about=test_data.get('about', ''),
                            fixed=test_data.get('fixed', False),
                            python=test_data.get('python', False),
                            env=test_env,
                            expect_stdout=_as_list(test_data.get('expect-stdout')),
                            reject_stdout=_as_list(test_data.get('reject-stdout')),
                            expect_stderr=_as_list(test_data.get('expect-stderr')),
                            reject_stderr=_as_list(test_data.get('reject-stderr')),
                        )

        config.tests_config = tests_config
        return config

    except yaml.YAMLError as e:
        print(f"Error parsing YAML file: {e}", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"YAML file not found: {config.yaml_file}", file=sys.stderr)
        sys.exit(1)

def validate_config(config: TestFrameworkConfig) -> None:
    """Validate the configuration."""
    if not os.path.isdir(config.fixture_dir):
        print(f"Fixture directory does not exist: {config.fixture_dir}", file=sys.stderr)
        sys.exit(1)
    Path(config.dump_dir).mkdir(parents=True, exist_ok=True)
    if not os.path.isdir(config.bin_dir):
        print(f"Binary directory does not exist: {config.bin_dir}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isdir(config.lib_dir):
        print(f"Library directory does not exist: {config.lib_dir}", file=sys.stderr)
        sys.exit(1)

    if not config.tests_config:
        print("No test configurations found for specified groups", file=sys.stderr)
        sys.exit(1)

def run_test(test_config: TestConfig, framework_config: TestFrameworkConfig) -> TestResult:
    """Run a single test binary and capture its output."""
    env = os.environ.copy()  # Copy current environment
    if test_config.env:
        env.update(test_config.env) # add/modify as per test

    if test_config.python and framework_config.py_dir:
        py_script = Path(framework_config.py_dir) / f"{test_config.binary}.py"
        cmd = ['python3', str(py_script), test_config.test_id, str(test_config.verbosity)]
        env['BIN_DIR'] = framework_config.bin_dir
    else:
        binary_path = Path(framework_config.bin_dir) / test_config.binary
        cmd = []
        if framework_config.use_valgrind and test_config.valgrind:
            cmd.extend(['valgrind', '--leak-check=full', '--show-leak-kinds=all'])
            if test_config.valgrind_args:
                for arg in test_config.valgrind_args:
                    if "--show-leak-kinds" in arg:
                        cmd.pop(-1)
                cmd.extend(test_config.valgrind_args)
            if test_config.valgrind_suppressions:
                pass
                """
                suppression_file = f"/tmp/{test_config.binary}_{test_config.test_id}_suppressions.txt"
                with open(suppression_file, 'w') as f:
                    f.write(test_config.valgrind_suppressions)
                cmd.extend(['--suppressions', suppression_file])
                """
        cmd.extend([str(binary_path), test_config.test_id, str(test_config.verbosity)])

    if framework_config.verbose:
        print(" ".join(cmd))

    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env
        )

        try:
            stdout, stderr = process.communicate(timeout=TIMEOUT)
            exit_code = process.returncode

            valgrind_error = None
            if framework_config.use_valgrind and test_config.valgrind and stderr:
                error_match = re.search(r'==\d+== ERROR SUMMARY: (\d+) errors from (\d+) contexts', stderr)
                if error_match:
                    errors, contexts = map(int, error_match.groups())
                    if errors > 0:
                        valgrind_error = f"{errors} errors from {contexts} contexts"
                else:
                    valgrind_error = "Unable to find valgrind error summary"

            return TestResult(
                command=" ".join(cmd),
                exit_code=exit_code,
                stdout=stdout,
                stderr=stderr,
                valgrind_error=valgrind_error
            )

        except subprocess.TimeoutExpired as e:
            if process:
                process.kill()
                try:
                    stdout, stderr = process.communicate()
                except:
                    stdout = e.stdout if e.stdout else ""
                    stderr = e.stderr if e.stderr else ""

            return TestResult(
                command=" ".join(cmd),
                exit_code=-100,
                stdout=stdout if stdout else "",
                stderr=stderr if stderr else "Test timed out",
                valgrind_error="Test timed out",
                timeout = True
            )

    except subprocess.CalledProcessError as e:
        return TestResult(
            command=" ".join(cmd),
            exit_code=e.returncode,
            stdout=e.stdout if e.stdout else "",
            stderr=e.stderr if e.stderr else "",
            valgrind_error="Process failed to execute"
        )
    except Exception as e:
        return TestResult(
            command=" ".join(cmd),
            exit_code=-1,
            stdout="",
            stderr=str(e),
            valgrind_error="Unexpected error during execution"
        )

def check_assertions(test_config: TestConfig, result: TestResult) -> Optional[str]:
    """Check output assertions (expect/reject for stdout/stderr). Returns error message or None."""
    errors = []
    if test_config.expect_stdout:
        for pattern in test_config.expect_stdout:
            if pattern not in result.stdout:
                errors.append(f"expected string not found in stdout: '{pattern}'")
    if test_config.reject_stdout:
        for pattern in test_config.reject_stdout:
            if pattern in result.stdout:
                errors.append(f"rejected string found in stdout: '{pattern}'")
    if test_config.expect_stderr:
        for pattern in test_config.expect_stderr:
            if pattern not in result.stderr:
                errors.append(f"expected string not found in stderr: '{pattern}'")
    if test_config.reject_stderr:
        for pattern in test_config.reject_stderr:
            if pattern in result.stderr:
                errors.append(f"rejected string found in stderr: '{pattern}'")
    return "; ".join(errors) if errors else None

def handle_fixture(config: TestFrameworkConfig, test_config: TestConfig, result: TestResult) -> Optional[str]:
    """Handle dump saving/comparison. Returns error message if comparison fails."""
    if not test_config.fixtures:
        return None

    dump_path = Path(config.dump_dir) / f"{test_config.binary}_{test_config.test_id}.out"

    if config.dump and (test_config.fixed == False or config.override):
        dump_path.write_text(result.stdout)
        return None
    elif config.use_fixtures:
        if not dump_path.exists():
            return f"Dump file not found: {dump_path}"

        expected = dump_path.read_text()
        if expected != result.stdout:
            expected_lines = expected.splitlines()
            actual_lines = result.stdout.splitlines()

            if len(expected_lines) != len(actual_lines):
                return f"Fixture mismatch: expected {len(expected_lines)} lines, got {len(actual_lines)}"

            for line_num, (exp_line, act_line) in enumerate(zip(expected_lines, actual_lines), 1):
                if exp_line != act_line:
                    return f"Fixture mismatch at line {line_num}:\nexpected: '{exp_line}'\ngot:      '{act_line}'"

    return None

def collect_tests(framework_config: TestFrameworkConfig) -> List[TestConfig]:
    """Collect all tests that need to be run based on the framework configuration."""
    tests = []
    for group, binaries in framework_config.tests_config.items():
        for binary, test_cases in binaries.items():
            for test_id, test_config in test_cases.items():
                tests.append(test_config)
    return tests

def run_single_test(test_config: TestConfig,
        framework_config: TestFrameworkConfig) -> Tuple[TestConfig, TestResult]:
    """Run a single test and return its result."""
    result = run_test(test_config, framework_config)

    # Handle fixtures if needed
    fixture_error = None
    if test_config.fixtures:
        fixture_error = handle_fixture(framework_config, test_config, result)

    # Check output assertions
    assertion_error = check_assertions(test_config, result)

    # Create new TestResult with fixture and assertion errors
    result = TestResult(
        command=result.command,
        exit_code=result.exit_code,
        stdout=result.stdout,
        stderr=result.stderr,
        valgrind_error=result.valgrind_error,
        fixture_error=fixture_error,
        assertion_error=assertion_error,
    )

    return test_config, result

def run_all_tests(framework_config: TestFrameworkConfig) -> List[Tuple[TestConfig, TestResult]]:
    """Run all tests in parallel and return their results."""
    tests = collect_tests(framework_config)
    for i, test in enumerate(tests):
        test.num=i+1

    results = []

    print(f"\nRunning {len(tests)} tests with max {framework_config.max_workers} parallel jobs...")

    with concurrent.futures.ThreadPoolExecutor(max_workers=framework_config.max_workers) as executor:
        future_to_test = {
            executor.submit(run_single_test, test, framework_config): test
            for test in tests
        }

        for i, future in enumerate(concurrent.futures.as_completed(future_to_test)):
            try:
                test_config, result = future.result()

                results.append((test_config, result))

                test_desc = f"{test_config.binary} {test_config.test_id}"
                if result.timeout:
                    print(f"❌ {test_config.num} {test_desc} timed out")
                elif result.exit_code != 0:
                    print(f"❌ {test_config.num} {test_desc} failed with exit code {result.exit_code}")
                elif result.valgrind_error:
                    print(f"❌ {test_config.num} {test_desc} had valgrind errors: {result.valgrind_error}")
                elif result.fixture_error:
                    print(f"❌ {test_config.num} {test_desc} fixture error: {result.fixture_error}")
                elif result.assertion_error:
                    print(f"❌ {test_config.num} {test_desc} assertion error: {result.assertion_error}")
                else:
                    print(f"✓ {test_config.num} {test_desc} passed")
            except concurrent.futures.TimeoutError:
                test = future_to_test[future]
                print(f"❌ {test_config.num} {test.binary} {test.test_id} timed out")
                future.cancel()

    return results

def print_final_summary(results: List[Tuple[TestConfig, TestResult]]) -> bool:
    """Print final test summary and return True if all tests passed."""
    print("\nTest Summary:")
    print("=" * 80)

    failed_tests = []
    for test_config, result in results:
        test_desc = f"{test_config.group}/{test_config.binary} {test_config.test_id}"
        if result.exit_code != 0:
            failed_tests.append((test_desc, f"Exit code {result.exit_code}"))
        elif result.valgrind_error:
            failed_tests.append((test_desc, f"Valgrind: {result.valgrind_error}"))
        elif result.fixture_error:
            failed_tests.append((test_desc, f"Fixture: {result.fixture_error}"))
        elif result.assertion_error:
            failed_tests.append((test_desc, f"Assertion: {result.assertion_error}"))

    print(f"Total tests: {len(results)}")
    print(f"Passed: {len(results) - len(failed_tests)}")
    print(f"Failed: {len(failed_tests)}")

    if failed_tests:
        print("\nFailed tests:")
        for test_desc, reason in failed_tests:
            print(f"  {test_desc}: {reason}")
        return False

    return True

def dump_output(config: TestFrameworkConfig, results: List[Tuple[TestConfig, TestResult]]):
    """Write detailed test output to a file."""
    with open(config.full_output, 'w') as f:
        for test_config, result in results:
            f.write(f"\n{'='*80}\n")
            f.write(f"Command: {result.command}\n")
            f.write(f"{'='*80}\n")
            if result.fixture_error:
                f.write(f"FIXTURE ERROR: {result.fixture_error}\n")
            f.write(f"STDOUT:\n{result.stdout}\n")
            f.write(f"STDERR:\n{result.stderr}\n")

def main():
    # Load and validate configuration
    config = parse_cli_args()
    config = load_yaml_config(config)
    validate_config(config)

    # Set environment variables
    os.environ.update(config.env_vars)

    # Run all tests
    results = run_all_tests(config)

    if config.full_output:
        dump_output(config, results)

    # Print final summary and exit with appropriate code
    success = print_final_summary(results)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
