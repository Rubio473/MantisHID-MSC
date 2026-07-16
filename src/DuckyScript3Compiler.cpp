#include "DuckyScript3Compiler.h"

#include <limits.h>

DuckyScript3Compiler::Value::Value() : isString(false), number(0), text("") {}

DuckyScript3Compiler::Value DuckyScript3Compiler::Value::fromNumber(int32_t value) {
    Value result;
    result.number = value;
    return result;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::Value::fromString(const String& value) {
    Value result;
    result.isString = true;
    result.text = value;
    return result;
}

bool DuckyScript3Compiler::Value::toBool() const {
    if (isString) return text.length() > 0 && text != "0" && !text.equalsIgnoreCase("FALSE");
    return number != 0;
}

int32_t DuckyScript3Compiler::Value::toNumber(bool& ok) const {
    if (!isString) {
        ok = true;
        return number;
    }
    if (text.length() == 0) {
        ok = false;
        return 0;
    }
    int start = 0;
    if (text[0] == '-' || text[0] == '+') start = 1;
    if (start >= static_cast<int>(text.length())) {
        ok = false;
        return 0;
    }
    int64_t parsed = 0;
    for (int i = start; i < static_cast<int>(text.length()); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            ok = false;
            return 0;
        }
        parsed = parsed * 10 + static_cast<int64_t>(c - '0');
        if (parsed > static_cast<int64_t>(INT32_MAX) + 1) {
            ok = false;
            return 0;
        }
    }
    if (text[0] == '-') parsed = -parsed;
    if (parsed < INT32_MIN || parsed > INT32_MAX) {
        ok = false;
        return 0;
    }
    ok = true;
    return static_cast<int32_t>(parsed);
}

String DuckyScript3Compiler::Value::toString() const {
    return isString ? text : String(number);
}

DuckyScript3Compiler::ExpressionParser::ExpressionParser(
    const String& expression,
    const std::map<String, Value>& values
) : input(expression), variables(values), position(0), parseError("") {}

bool DuckyScript3Compiler::ExpressionParser::parse(Value& result, String& error) {
    result = parseOr();
    skipSpaces();
    if (!hasError() && !atEnd()) fail("unexpected token");
    error = parseError;
    return !hasError();
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseOr() {
    Value left = parseAnd();
    while (!hasError()) {
        skipSpaces();
        if (consume("||") || consumeKeyword("OR")) {
            Value right = parseAnd();
            left = Value::fromNumber(left.toBool() || right.toBool());
        } else {
            break;
        }
    }
    return left;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseAnd() {
    Value left = parseComparison();
    while (!hasError()) {
        skipSpaces();
        if (consume("&&") || consumeKeyword("AND")) {
            Value right = parseComparison();
            left = Value::fromNumber(left.toBool() && right.toBool());
        } else {
            break;
        }
    }
    return left;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseComparison() {
    Value left = parseAdditive();
    while (!hasError()) {
        skipSpaces();
        String op;
        if (consume("==")) op = "==";
        else if (consume("!=")) op = "!=";
        else if (consume("<=")) op = "<=";
        else if (consume(">=")) op = ">=";
        else if (consume("<")) op = "<";
        else if (consume(">")) op = ">";
        else break;
        Value right = parseAdditive();
        left = applyBinary(op, left, right);
    }
    return left;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseAdditive() {
    Value left = parseMultiplicative();
    while (!hasError()) {
        skipSpaces();
        String op;
        if (consume("+")) op = "+";
        else if (consume("-")) op = "-";
        else break;
        Value right = parseMultiplicative();
        left = applyBinary(op, left, right);
    }
    return left;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseMultiplicative() {
    Value left = parseUnary();
    while (!hasError()) {
        skipSpaces();
        String op;
        if (consume("*")) op = "*";
        else if (consume("/")) op = "/";
        else if (consume("%")) op = "%";
        else break;
        Value right = parseUnary();
        left = applyBinary(op, left, right);
    }
    return left;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parseUnary() {
    skipSpaces();
    if (consume("!") || consumeKeyword("NOT")) return Value::fromNumber(!parseUnary().toBool());
    if (consume("-")) {
        Value value = parseUnary();
        bool ok = false;
        const int32_t number = value.toNumber(ok);
        if (!ok) {
            fail("unary minus needs a number");
            return Value();
        }
        if (number == INT32_MIN) {
            fail("integer overflow");
            return Value();
        }
        return Value::fromNumber(-number);
    }
    if (consume("+")) return parseUnary();
    return parsePrimary();
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::parsePrimary() {
    skipSpaces();
    if (consume("(")) {
        Value value = parseOr();
        skipSpaces();
        if (!consume(")")) fail("missing closing parenthesis");
        return value;
    }
    if (position < static_cast<int>(input.length()) && (input[position] == '"' || input[position] == '\'')) {
        return Value::fromString(parseQuotedString());
    }
    if (position < static_cast<int>(input.length()) && input[position] == '$') {
        String name = parseIdentifier();
        auto found = variables.find(name);
        if (found == variables.end()) {
            fail("unknown variable " + name);
            return Value();
        }
        return found->second;
    }
    if (consumeKeyword("TRUE")) return Value::fromNumber(1);
    if (consumeKeyword("FALSE")) return Value::fromNumber(0);
    if (position < static_cast<int>(input.length()) && input[position] >= '0' && input[position] <= '9') {
        int64_t parsed = 0;
        while (position < static_cast<int>(input.length()) && input[position] >= '0' && input[position] <= '9') {
            parsed = parsed * 10 + static_cast<int64_t>(input[position] - '0');
            if (parsed > INT32_MAX) {
                fail("integer overflow");
                return Value();
            }
            position++;
        }
        return Value::fromNumber(static_cast<int32_t>(parsed));
    }
    const int start = position;
    while (position < static_cast<int>(input.length())) {
        const char c = input[position];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ')' || c == '(' ||
            c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '<' || c == '>' ||
            c == '=' || c == '!' || c == '&' || c == '|') break;
        position++;
    }
    if (position > start) return Value::fromString(input.substring(start, position));
    fail("expected value");
    return Value();
}

void DuckyScript3Compiler::ExpressionParser::skipSpaces() {
    while (position < static_cast<int>(input.length()) &&
           (input[position] == ' ' || input[position] == '\t' || input[position] == '\r' || input[position] == '\n')) {
        position++;
    }
}

bool DuckyScript3Compiler::ExpressionParser::consume(const char* token) {
    skipSpaces();
    const String target(token);
    if (input.substring(position, position + static_cast<int>(target.length())) == target) {
        position += static_cast<int>(target.length());
        return true;
    }
    return false;
}

bool DuckyScript3Compiler::ExpressionParser::consumeKeyword(const char* token) {
    skipSpaces();
    const int start = position;
    const String target(token);
    if (start + static_cast<int>(target.length()) > static_cast<int>(input.length())) return false;
    String candidate = input.substring(start, start + static_cast<int>(target.length()));
    candidate.toUpperCase();
    String expected(target);
    expected.toUpperCase();
    if (candidate != expected) return false;
    const int end = start + static_cast<int>(target.length());
    if (end < static_cast<int>(input.length())) {
        const char c = input[end];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') return false;
    }
    position = end;
    return true;
}

bool DuckyScript3Compiler::ExpressionParser::atEnd() {
    return position >= static_cast<int>(input.length());
}

bool DuckyScript3Compiler::ExpressionParser::hasError() const {
    return parseError.length() > 0;
}

void DuckyScript3Compiler::ExpressionParser::fail(const String& message) {
    if (parseError.length() == 0) parseError = message;
}

String DuckyScript3Compiler::ExpressionParser::parseIdentifier() {
    skipSpaces();
    const int start = position;
    if (position < static_cast<int>(input.length()) && input[position] == '$') position++;
    while (position < static_cast<int>(input.length())) {
        const char c = input[position];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) break;
        position++;
    }
    String result = input.substring(start, position);
    result.toUpperCase();
    return result;
}

String DuckyScript3Compiler::ExpressionParser::parseQuotedString() {
    const char quote = input[position++];
    String result;
    while (position < static_cast<int>(input.length())) {
        char c = input[position++];
        if (c == quote) return result;
        if (c == '\\' && position < static_cast<int>(input.length())) {
            const char escaped = input[position++];
            if (escaped == 'n') result += "\n";
            else if (escaped == 'r') result += "\r";
            else if (escaped == 't') result += "\t";
            else result += String(escaped);
        } else {
            result += String(c);
        }
    }
    fail("unterminated string");
    return result;
}

DuckyScript3Compiler::Value DuckyScript3Compiler::ExpressionParser::applyBinary(
    const String& op,
    const Value& left,
    const Value& right
) {
    if (op == "+" && (left.isString || right.isString)) return Value::fromString(left.toString() + right.toString());

    bool leftOk = false;
    bool rightOk = false;
    const int32_t leftNumber = left.toNumber(leftOk);
    const int32_t rightNumber = right.toNumber(rightOk);

    if (op == "==" || op == "!=") {
        bool equal;
        if (leftOk && rightOk) equal = leftNumber == rightNumber;
        else equal = left.toString() == right.toString();
        return Value::fromNumber(op == "==" ? equal : !equal);
    }

    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        int comparison = 0;
        if (leftOk && rightOk) comparison = leftNumber < rightNumber ? -1 : (leftNumber > rightNumber ? 1 : 0);
        else comparison = left.toString().compareTo(right.toString());
        if (op == "<") return Value::fromNumber(comparison < 0);
        if (op == "<=") return Value::fromNumber(comparison <= 0);
        if (op == ">") return Value::fromNumber(comparison > 0);
        return Value::fromNumber(comparison >= 0);
    }

    if (!leftOk || !rightOk) {
        fail("numeric operator needs numbers");
        return Value();
    }

    int64_t result = 0;
    if (op == "+") result = static_cast<int64_t>(leftNumber) + rightNumber;
    else if (op == "-") result = static_cast<int64_t>(leftNumber) - rightNumber;
    else if (op == "*") result = static_cast<int64_t>(leftNumber) * rightNumber;
    else if (op == "/") {
        if (rightNumber == 0) {
            fail("division by zero");
            return Value();
        }
        if (leftNumber == INT32_MIN && rightNumber == -1) {
            fail("integer overflow");
            return Value();
        }
        result = leftNumber / rightNumber;
    } else if (op == "%") {
        if (rightNumber == 0) {
            fail("division by zero");
            return Value();
        }
        result = leftNumber % rightNumber;
    }

    if (result < INT32_MIN || result > INT32_MAX) {
        fail("integer overflow");
        return Value();
    }
    return Value::fromNumber(static_cast<int32_t>(result));
}

DuckyScript3Compiler::DuckyScript3Compiler()
    : compileErrorLine(0), outputLines(0), loopIterations(0), functionCalls(0) {}

bool DuckyScript3Compiler::compile(const String& source, String& output, String& error, uint32_t& errorLine) {
    lines.clear();
    values.clear();
    constants.clear();
    functions.clear();
    compiled = "";
    compileError = "";
    compileErrorLine = 0;
    outputLines = 0;
    loopIterations = 0;
    functionCalls = 0;

    if (!splitLines(source) || !collectFunctions()) {
        output = "";
        error = compileError;
        errorLine = compileErrorLine;
        return false;
    }

    bool returned = false;
    if (!executeRange(0, static_cast<int>(lines.size()), 0, false, returned)) {
        output = "";
        error = compileError;
        errorLine = compileErrorLine;
        return false;
    }

    output = compiled;
    error = "OK";
    errorLine = 0;
    return true;
}

bool DuckyScript3Compiler::splitLines(const String& source) {
    int start = 0;
    while (start <= static_cast<int>(source.length())) {
        int end = source.indexOf('\n', start);
        if (end < 0) end = source.length();
        String line = source.substring(start, end);
        if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
        lines.push_back(line);
        if (lines.size() > MAX_SOURCE_LINES) {
            fail(static_cast<uint32_t>(lines.size()), "source exceeds 1024 lines");
            return false;
        }
        if (end >= static_cast<int>(source.length())) break;
        start = end + 1;
    }
    return true;
}

bool DuckyScript3Compiler::collectFunctions() {
    bool commentBlock = false;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        String line = lines[i];
        line.trim();
        String upper = line;
        upper.toUpperCase();
        const String command = commandOf(line);
        if (commentBlock) {
            if (upper == "REM_BLOCK END") commentBlock = false;
            continue;
        }
        if (command == "REM_BLOCK") {
            if (upper != "REM_BLOCK END") commentBlock = true;
            continue;
        }
        if (command == "REM") continue;
        if (command != "FUNCTION") continue;
        String name = parametersOf(line);
        name.trim();
        if (name.endsWith("()")) name = name.substring(0, name.length() - 2);
        name = normalizeName(name);
        if (name.length() == 0 || name[0] == '$') {
            fail(i + 1, "invalid function name");
            return false;
        }
        if (functions.size() >= MAX_FUNCTIONS) {
            fail(i + 1, "too many functions");
            return false;
        }
        int depth = 1;
        int end = i + 1;
        for (; end < static_cast<int>(lines.size()); ++end) {
            const String command = commandOf(lines[end]);
            if (command == "FUNCTION") depth++;
            else if (command == "END_FUNCTION") {
                depth--;
                if (depth == 0) break;
            }
        }
        if (end >= static_cast<int>(lines.size())) {
            fail(i + 1, "FUNCTION missing END_FUNCTION");
            return false;
        }
        if (depth != 0) {
            fail(i + 1, "invalid FUNCTION nesting");
            return false;
        }
        FunctionDef definition;
        definition.start = i + 1;
        definition.end = end;
        definition.declaration = i;
        functions[name] = definition;
        i = end;
    }
    return true;
}

bool DuckyScript3Compiler::executeRange(
    int start,
    int end,
    uint32_t depth,
    bool allowReturn,
    bool& returned
) {
    if (depth > MAX_NESTING) {
        fail(start + 1, "nesting exceeds 12 levels");
        return false;
    }

    bool commentBlock = false;
    for (int i = start; i < end; ++i) {
        String line = lines[i];
        line.trim();
        if (line.length() == 0) continue;
        String upper = line;
        upper.toUpperCase();
        const String command = commandOf(line);

        if (commentBlock) {
            if (!appendOutput(line, i + 1)) return false;
            if (upper == "REM_BLOCK END") commentBlock = false;
            continue;
        }
        if (command == "REM_BLOCK") {
            if (!appendOutput(line, i + 1)) return false;
            if (upper != "REM_BLOCK END") commentBlock = true;
            continue;
        }
        if (command == "REM") {
            if (!appendOutput(line, i + 1)) return false;
            continue;
        }

        if (command == "FUNCTION") {
            int functionEnd = findMatchingEnd(i, end, "FUNCTION", "END_FUNCTION");
            if (functionEnd < 0) {
                fail(i + 1, "FUNCTION missing END_FUNCTION");
                return false;
            }
            i = functionEnd;
            continue;
        }
        if (command == "IF") {
            if (!executeIf(i, end, depth + 1, allowReturn, returned)) return false;
            if (returned) return true;
            continue;
        }
        if (command == "WHILE") {
            if (!executeWhile(i, end, depth + 1, allowReturn, returned)) return false;
            if (returned) return true;
            continue;
        }
        if (command == "RETURN") {
            if (!allowReturn) {
                fail(i + 1, "RETURN outside FUNCTION");
                return false;
            }
            returned = true;
            return true;
        }
        if (command == "ELSE" || command == "ELSE_IF" || command == "END_IF" ||
            command == "END_WHILE" || command == "END_FUNCTION") {
            fail(i + 1, "unexpected " + command);
            return false;
        }

        bool handled = false;
        if (!handleVariableLine(line, i + 1, handled)) return false;
        if (handled) continue;

        String functionName;
        if (isFunctionCall(line, functionName)) {
            if (!executeFunction(functionName, depth + 1)) {
                if (compileError.length() == 0) fail(i + 1, "unknown function " + functionName);
                return false;
            }
            continue;
        }

        bool expandedOk = false;
        const String expanded = expandVariables(line, i + 1, expandedOk);
        if (!expandedOk || !appendOutput(expanded, i + 1)) return false;
    }
    return true;
}

bool DuckyScript3Compiler::executeIf(
    int& index,
    int end,
    uint32_t depth,
    bool allowReturn,
    bool& returned
) {
    struct Branch {
        String condition;
        int start;
        int end;
        bool isElse;
    };

    std::vector<Branch> branches;
    String currentCondition = parametersOf(lines[index]);
    int branchStart = index + 1;
    int nesting = 1;
    int closing = -1;

    for (int i = index + 1; i < end; ++i) {
        const String command = commandOf(lines[i]);
        if (command == "IF") {
            nesting++;
            continue;
        }
        if (command == "END_IF") {
            nesting--;
            if (nesting == 0) {
                Branch branch{currentCondition, branchStart, i, currentCondition.length() == 0};
                branches.push_back(branch);
                closing = i;
                break;
            }
            continue;
        }
        if (nesting == 1 && (command == "ELSE_IF" || command == "ELSE")) {
            Branch branch{currentCondition, branchStart, i, currentCondition.length() == 0};
            branches.push_back(branch);
            currentCondition = command == "ELSE" ? String("") : parametersOf(lines[i]);
            branchStart = i + 1;
        }
    }

    if (closing < 0) {
        fail(index + 1, "IF missing END_IF");
        return false;
    }

    for (const Branch& branch : branches) {
        bool selected = branch.isElse;
        if (!branch.isElse) {
            Value condition;
            if (!evaluate(branch.condition, condition, index + 1)) return false;
            selected = condition.toBool();
        }
        if (selected) {
            if (!executeRange(branch.start, branch.end, depth, allowReturn, returned)) return false;
            break;
        }
    }

    index = closing;
    return true;
}

bool DuckyScript3Compiler::executeWhile(
    int& index,
    int end,
    uint32_t depth,
    bool allowReturn,
    bool& returned
) {
    const int closing = findMatchingEnd(index, end, "WHILE", "END_WHILE");
    if (closing < 0) {
        fail(index + 1, "WHILE missing END_WHILE");
        return false;
    }
    const String conditionText = parametersOf(lines[index]);
    while (true) {
        Value condition;
        if (!evaluate(conditionText, condition, index + 1)) return false;
        if (!condition.toBool()) break;
        loopIterations++;
        if (loopIterations > MAX_LOOP_ITERATIONS) {
            fail(index + 1, "loop limit exceeded");
            return false;
        }
        if (!executeRange(index + 1, closing, depth, allowReturn, returned)) return false;
        if (returned) break;
    }
    index = closing;
    return true;
}

bool DuckyScript3Compiler::executeFunction(const String& name, uint32_t depth) {
    auto found = functions.find(normalizeName(name));
    if (found == functions.end()) return false;
    functionCalls++;
    if (functionCalls > MAX_FUNCTION_CALLS) {
        fail(found->second.declaration + 1, "function call limit exceeded");
        return false;
    }
    bool returned = false;
    return executeRange(found->second.start, found->second.end, depth, true, returned);
}

bool DuckyScript3Compiler::handleVariableLine(const String& line, uint32_t lineNumber, bool& handled) {
    handled = false;
    const String command = commandOf(line);
    bool declaring = command == "VAR" || command == "DEFINE";
    String expressionLine = declaring ? parametersOf(line) : line;
    expressionLine.trim();

    int equals = expressionLine.indexOf('=');
    String name;
    String expression;

    if (declaring) {
        if (equals >= 0) {
            name = expressionLine.substring(0, equals);
            expression = expressionLine.substring(equals + 1);
        } else {
            const int space = expressionLine.indexOf(' ');
            if (space >= 0) {
                name = expressionLine.substring(0, space);
                expression = expressionLine.substring(space + 1);
            } else {
                name = expressionLine;
                expression = "0";
            }
        }
    } else {
        if (!expressionLine.startsWith("$") || equals < 0) return true;
        name = expressionLine.substring(0, equals);
        expression = expressionLine.substring(equals + 1);
    }

    handled = true;
    name = normalizeName(name);
    expression.trim();
    if (name.length() < 2 || name[0] != '$') {
        fail(lineNumber, "invalid variable name");
        return false;
    }
    for (size_t i = 1; i < name.length(); ++i) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            fail(lineNumber, "invalid variable name");
            return false;
        }
    }
    if (!declaring && constants.find(name) != constants.end()) {
        fail(lineNumber, "cannot assign constant " + name);
        return false;
    }
    if (values.find(name) == values.end() && values.size() >= MAX_VARIABLES) {
        fail(lineNumber, "too many variables");
        return false;
    }

    Value value;
    if (!evaluate(expression, value, lineNumber)) return false;
    values[name] = value;
    if (command == "DEFINE") constants[name] = true;
    return true;
}

bool DuckyScript3Compiler::evaluate(const String& expression, Value& result, uint32_t lineNumber) {
    String value = expression;
    value.trim();
    if (value.length() == 0) {
        fail(lineNumber, "missing expression");
        return false;
    }
    ExpressionParser parser(value, values);
    String error;
    if (!parser.parse(result, error)) {
        fail(lineNumber, error);
        return false;
    }
    return true;
}

bool DuckyScript3Compiler::appendOutput(const String& line, uint32_t lineNumber) {
    if (line.length() == 0) return true;
    outputLines++;
    if (outputLines > MAX_OUTPUT_LINES) {
        fail(lineNumber, "compiled script exceeds 2048 lines");
        return false;
    }
    if (compiled.length() + line.length() + 1 > MAX_OUTPUT_CHARS) {
        fail(lineNumber, "compiled script exceeds 64 KiB");
        return false;
    }
    compiled += line;
    compiled += "\n";
    return true;
}

String DuckyScript3Compiler::expandVariables(const String& line, uint32_t lineNumber, bool& ok) {
    String result;
    for (int i = 0; i < static_cast<int>(line.length());) {
        if (line[i] != '$') {
            result += String(line[i++]);
            continue;
        }
        const int start = i++;
        while (i < static_cast<int>(line.length())) {
            const char c = line[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) break;
            i++;
        }
        if (i == start + 1) {
            result += "$";
            continue;
        }
        String name = line.substring(start, i);
        name = normalizeName(name);
        auto found = values.find(name);
        if (found == values.end()) {
            fail(lineNumber, "unknown variable " + name);
            ok = false;
            return "";
        }
        result += found->second.toString();
    }
    ok = true;
    return result;
}

String DuckyScript3Compiler::commandOf(const String& source) const {
    String line = source;
    line.trim();
    if (line.length() == 0) return "";
    String upper = line;
    upper.toUpperCase();
    if (upper.startsWith("ELSE IF ")) return "ELSE_IF";
    const int space = upper.indexOf(' ');
    return space >= 0 ? upper.substring(0, space) : upper;
}

String DuckyScript3Compiler::parametersOf(const String& source) const {
    String line = source;
    line.trim();
    String upper = line;
    upper.toUpperCase();
    if (upper.startsWith("ELSE IF ")) return line.substring(8);
    const int space = line.indexOf(' ');
    return space >= 0 ? line.substring(space + 1) : String("");
}

String DuckyScript3Compiler::normalizeName(const String& source) const {
    String name = source;
    name.trim();
    name.toUpperCase();
    return name;
}

bool DuckyScript3Compiler::isFunctionCall(const String& source, String& functionName) const {
    String line = source;
    line.trim();
    if (!line.endsWith("()")) return false;
    if (line.indexOf(' ') >= 0 || line.indexOf('\t') >= 0) return false;
    functionName = normalizeName(line.substring(0, line.length() - 2));
    return functionName.length() > 0;
}

int DuckyScript3Compiler::findMatchingEnd(
    int start,
    int end,
    const String& opener,
    const String& closer
) const {
    int nesting = 1;
    for (int i = start + 1; i < end; ++i) {
        const String command = commandOf(lines[i]);
        if (command == opener) nesting++;
        else if (command == closer) {
            nesting--;
            if (nesting == 0) return i;
        }
    }
    return -1;
}

void DuckyScript3Compiler::fail(uint32_t lineNumber, const String& message) {
    if (compileError.length() > 0) return;
    compileErrorLine = lineNumber;
    compileError = message;
}
