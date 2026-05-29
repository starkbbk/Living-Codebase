import ast
import copy
import random
from typing import List

class ASTMutator:
    """
    AST Mutation Engine for the Genetic Algorithm
    Operates on Abstract Syntax Trees to insert safety constructs and fix execution faults.
    """
    
    MUTATION_OPS = [
        "swap_operator",
        "insert_null_check", 
        "insert_bounds_check",
        "replace_literal",
        "swap_comparison",
        "add_early_return",
    ]
    
    def __init__(self, source_code: str):
        self.source = source_code
        self.original_ast = ast.parse(source_code)
    
    def mutate(self, tree: ast.AST, op: str) -> ast.AST:
        """Apply a single mutation operation to the AST"""
        mutated = copy.deepcopy(tree)
        
        if op == "insert_null_check":
            return self._insert_null_check(mutated)
        elif op == "swap_operator":
            return self._swap_operator(mutated)
        elif op == "insert_bounds_check":
            return self._insert_bounds_check(mutated)
        elif op == "add_early_return":
            return self._add_early_return(mutated)
        elif op == "replace_literal":
            return self._replace_literal(mutated)
        elif op == "swap_comparison":
            return self._swap_comparison(mutated)
        
        return mutated
    
    def _insert_null_check(self, tree: ast.AST) -> ast.AST:
        """Find variable loads/attributes and insert non-None/NULL validation guards"""
        class NullCheckInserter(ast.NodeTransformer):
            def visit_Attribute(self, node):
                # Check if this attribute access can be guarded: obj.attr -> obj.attr if obj is not None else 0
                self.generic_visit(node)
                check = ast.IfExp(
                    test=ast.Compare(
                        left=node.value,
                        ops=[ast.IsNot()],
                        comparators=[ast.Constant(None)]
                    ),
                    body=node,
                    orelse=ast.Constant(0)
                )
                return check
                
            def visit_Subscript(self, node):
                # Guard subscript pointer targets: array[idx] -> array[idx] if array is not None else 0
                self.generic_visit(node)
                check = ast.IfExp(
                    test=ast.Compare(
                        left=node.value,
                        ops=[ast.IsNot()],
                        comparators=[ast.Constant(None)]
                    ),
                    body=node,
                    orelse=ast.Constant(0)
                )
                return check
                
        return NullCheckInserter().visit(tree)
    
    def _swap_operator(self, tree: ast.AST) -> ast.AST:
        """Swap standard arithmetic operations to repair overflow / division faults"""
        swap_map = {
            ast.Add: ast.Sub,
            ast.Sub: ast.Add,
            ast.Mult: ast.Div,
            ast.Div: ast.Mult,
        }
        
        class OpSwapper(ast.NodeTransformer):
            def visit_BinOp(self, node):
                self.generic_visit(node)
                if type(node.op) in swap_map:
                    node.op = swap_map[type(node.op)]()
                return node
                
        return OpSwapper().visit(tree)
    
    def _insert_bounds_check(self, tree: ast.AST) -> ast.AST:
        """Insert array bounds checks on list/string subscript index offsets"""
        class BoundsCheckInserter(ast.NodeTransformer):
            def visit_Subscript(self, node):
                self.generic_visit(node)
                # Safeguard subscript access to stay within length:
                # array[idx] if len(array) > idx and idx >= 0 else 0
                len_call = ast.Call(
                    func=ast.Name(id='len', ctx=ast.Load()),
                    args=[node.value],
                    keywords=[]
                )
                test = ast.BoolOp(
                    op=ast.And(),
                    values=[
                        ast.Compare(
                            left=len_call,
                            ops=[ast.Gt()],
                            comparators=[node.slice]
                        ),
                        ast.Compare(
                            left=node.slice,
                            ops=[ast.GtE()],
                            comparators=[ast.Constant(0)]
                        )
                    ]
                )
                check = ast.IfExp(
                    test=test,
                    body=node,
                    orelse=ast.Constant(0)
                )
                return check
                
        return BoundsCheckInserter().visit(tree)
    
    def _add_early_return(self, tree: ast.AST) -> ast.AST:
        """Add short-circuit safe return paths in function entries"""
        class EarlyReturnAdder(ast.NodeTransformer):
            def visit_FunctionDef(self, node):
                self.generic_visit(node)
                # Inject a safe fallback check at function beginning
                guard = ast.If(
                    test=ast.Compare(
                        left=ast.Name(id='data', ctx=ast.Load()),
                        ops=[ast.Is()],
                        comparators=[ast.Constant(None)]
                    ),
                    body=[ast.Return(value=ast.Constant(-1))],
                    orelse=[]
                )
                # Inject into the start of the function body
                node.body.insert(0, guard)
                return node
                
        return EarlyReturnAdder().visit(tree)

    def _replace_literal(self, tree: ast.AST) -> ast.AST:
        """Mutate constants to discover optimal threshold limits"""
        class LiteralReplacer(ast.NodeTransformer):
            def visit_Constant(self, node):
                if isinstance(node.value, int):
                    # Increment, decrement, or zero numerical constants
                    node.value += random.choice([-1, 1, -node.value])
                return node
        return LiteralReplacer().visit(tree)

    def _swap_comparison(self, tree: ast.AST) -> ast.AST:
        """Invert logical bounds check direction"""
        swap_map = {
            ast.Lt: ast.LtE,
            ast.Gt: ast.GtE,
            ast.LtE: ast.Lt,
            ast.GtE: ast.Gt,
            ast.Eq: ast.NotEq,
            ast.NotEq: ast.Eq
        }
        
        class ComparisonSwapper(ast.NodeTransformer):
            def visit_Compare(self, node):
                self.generic_visit(node)
                new_ops = []
                for op in node.ops:
                    if type(op) in swap_map:
                        new_ops.append(swap_map[type(op)]())
                    else:
                        new_ops.append(op)
                node.ops = new_ops
                return node
        return ComparisonSwapper().visit(tree)

    def generate_population(self, size: int = 50) -> List[ast.AST]:
        """Synthesize initial diversified population for genetic iterations"""
        population = []
        for _ in range(size):
            ops = random.sample(self.MUTATION_OPS, k=random.randint(1, 2))
            candidate = copy.deepcopy(self.original_ast)
            for op in ops:
                candidate = self.mutate(candidate, op)
            population.append(candidate)
        return population
