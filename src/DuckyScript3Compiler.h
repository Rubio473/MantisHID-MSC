#ifndef DUCKYSCRIPT3_COMPILER_H
#define DUCKYSCRIPT3_COMPILER_H

#include <Arduino.h>
#include <map>
#include <vector>

class DuckyScript3Compiler {
public:
    DuckyScript3Compiler();
    bool compile(const String& source, String& output, String& error, uint32_t& errorLine);

private:
    struct Value {
        bool isString;
        int32_t number;
        String text;

        Value();
        static Value fromNumber(int32_t value);
        static Value fromString(const String& value);
        bool toBool() const;
        int32_t toNumber(bool& ok) const;
        String toString() const;
    };

    struct FunctionDef {
        int start;
        int end;
        int declaration;
    };

    class ExpressionParser {
    public:
        ExpressionParser(const String& expression, const std::map<String, Value>& values);
        bool parse(Value& result, String& error);

    private:
        const String input;
        const std::map<String, Value>& variables;
        int position;
        String parseError;

        Value parseOr();
        Value parseAnd();
        Value parseComparison();
        Value parseAdditive();
        Value parseMultiplicative();
        Value parseUnary();
        Value parsePrimary();
        void skipSpaces();
        bool consume(const char* token);
        bool consumeKeyword(const char* token);
        bool atEnd();
        bool hasError() const;
        void fail(const String& message);
        String parseIdentifier();
        String parseQuotedString();
        Value applyBinary(const String& op, const Value& left, const Value& right);
    };

    static constexpr uint32_t MAX_SOURCE_LINES = 1024;
    static constexpr uint32_t MAX_OUTPUT_LINES = 2048;
    static constexpr uint32_t MAX_OUTPUT_CHARS = 65536;
    static constexpr uint32_t MAX_VARIABLES = 64;
    static constexpr uint32_t MAX_FUNCTIONS = 32;
    static constexpr uint32_t MAX_NESTING = 12;
    static constexpr uint32_t MAX_LOOP_ITERATIONS = 1000;
    static constexpr uint32_t MAX_FUNCTION_CALLS = 256;

    std::vector<String> lines;
    std::map<String, Value> values;
    std::map<String, bool> constants;
    std::map<String, FunctionDef> functions;
    String compiled;
    String compileError;
    uint32_t compileErrorLine;
    uint32_t outputLines;
    uint32_t loopIterations;
    uint32_t functionCalls;

    bool splitLines(const String& source);
    bool collectFunctions();
    bool executeRange(int start, int end, uint32_t depth, bool allowReturn, bool& returned);
    bool executeIf(int& index, int end, uint32_t depth, bool allowReturn, bool& returned);
    bool executeWhile(int& index, int end, uint32_t depth, bool allowReturn, bool& returned);
    bool executeFunction(const String& name, uint32_t depth);
    bool handleVariableLine(const String& line, uint32_t lineNumber, bool& handled);
    bool evaluate(const String& expression, Value& result, uint32_t lineNumber);
    bool appendOutput(const String& line, uint32_t lineNumber);
    String expandVariables(const String& line, uint32_t lineNumber, bool& ok);
    String commandOf(const String& line) const;
    String parametersOf(const String& line) const;
    String normalizeName(const String& name) const;
    bool isFunctionCall(const String& line, String& functionName) const;
    int findMatchingEnd(int start, int end, const String& opener, const String& closer) const;
    void fail(uint32_t lineNumber, const String& message);
};

#endif
