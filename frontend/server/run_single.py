import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../solver'))

from solver import solve_case

if __name__ == '__main__':
    case_dir = sys.argv[1]
    solve_case(case_dir)
