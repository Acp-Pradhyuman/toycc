# Toy C (`tc`) Compiler

A hobby C-like compiler project — built with **GCC**, targeting **x86 assembly** via **NASM**. The `tc` compiler includes a complete pipeline: **Lexing**, **Parsing**, **Semantic Analysis**, **Code Optimization**, and **Assembly Code Generation**.

---

## 🚀 Features

### 🔤 Lexer
Generates tokens from source code. Supports:
- **Keywords**: `exit`, `int`, `if`, `else`, `while`, `do`
- **Identifiers**
- **Integer literals** in:
  - Decimal
  - Hexadecimal (`0x...`)
  - Binary (`0b...`)
  - Octal (`0...`)

### 🧠 Parser + Semantic Analyzer
Combines syntax parsing and semantic validation with several code optimizations:
- **Block-scoped symbol table management**
  - Symbol tables are created and destroyed within block scopes (`{ }`)
- **Tree Representation**:
  - Uses **Left-Child Right-Sibling (LCRS)** structure:
    - Each statement node points to its first child (leftmost child)
    - Subsequent statements in the same scope are linked as siblings (right pointers)
- **Optimizations**:
  - Constant folding
  - Copy propagation
  - Constant propagation
  - Loop unrolling

### ⚙️ Code Generator
- Directly generates **x86 assembly** using NASM syntax
- **Dead code elimination**:
  - Picks only the **first reachable `exit`** statement, discards unreachable code

---

## 📜 Grammar Specification (BNF Format)

```bnf
<program>         ::= <statements>

<statements>      ::= <block_statement> 
                    | <else_statement> 
                    | <variable_declaration> 
                    | <variable_assignment>
                    | <while_statement> 
                    | <do_while_statement>

<block_statement> ::= "{" <statements> "}"

<else_statement>  ::= <if_statement> <else_if_statement>* [else <block_statement>]

<if_statement>    ::= "if" "(" <expression> ")" <block_statement>

<else_if_statement> ::= "else if" "(" <expression> ")" <block_statement>

<expression>      ::= <primary_expression> <op> <expression>
                    | <primary_expression>

<primary_expression> ::= INT_Literal
                       | Identifier
                       | "(" <expression> ")"

<op>             ::= "*" | "/" | "%" 
                    | "+" | "-" 
                    | "<<" | ">>" 
                    | "<" | "<=" | ">" | ">=" 
                    | "==" | "!=" 
                    | "&" | "^" | "&&" | "||"

<variable_declaration> ::= "int" Identifier ";"
                         | "int" Identifier "=" <expression> ";"

<variable_assignment> ::= Identifier <assignment_op> <expression> ";"

<assignment_op>  ::= "=" | "+=" | "-=" | "*=" | "/=" 
                   | "%=" | "<<=" | ">>="

<while_statement> ::= "while" "(" <expression> ")" <block_statement>

<do_while_statement> ::= "do" <block_statement> "while" "(" <expression> ")" ";"
```

## Build Requirements

- GCC
- NASM
- Make

## Building

1. Clone the repository
2. Run `make` to build the compiler
3. Use `make clean` to remove build artifacts

## Known Issues

- Memory management: Current version shows memory leaks in valgrind (e.g., test14.tc shows ~1.3MB lost in ~32k blocks after 25k iterations)
- Looking for collaborators to help identify and fix memory leaks

## Future Improvements

- Complete elimination of memory leaks
- Additional optimizations
- Expanded language features

## 🤝 Contributing

This is currently a solo project, but I'd love to collaborate with others to improve it! 🚀

### Areas Needing Help

| Feature/Area              | Description                                                                 |
|---------------------------|-----------------------------------------------------------------------------|
| **Memory Leak Detection** | Valgrind reports leaks (e.g., `test14.tc` loses ~1.3MB after 25k iterations) |
| **printf Implementation** | Core output functionality needs to be implemented                          |
| **scanf Implementation**  | Input handling system not yet developed                                    |
| **Optimizations**         | **Existing**: constant folding, dead code elimination, peephole optims<br>**Seeking**: Deeper improvements or alternative approaches |
| **Testing & Bug Fixes**   | Edge-case handling, stability improvements, additional test cases          |

### How to Contribute

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature`)
3. Commit your changes (`git commit -m 'Add some feature'`)
4. Push to the branch (`git push origin feature/your-feature`)
5. Open a Pull Request (PR)

PRs are welcome! If you're interested in compilers or memory management, let's build something great together.

---

✨ *First time contributing?* Feel free to ask questions by opening an issue!

## 📜 License

MIT License

```text
Copyright (c) [2025] [Pradhyumna Thekkan]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```