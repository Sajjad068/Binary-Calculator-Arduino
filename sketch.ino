
#include <LiquidCrystal_I2C.h>
#include <ArduinoQueue.h>
#include <SimpleStack.h>
#include <Wire.h>

// -------------------- Constants --------------------
#define BAD_RESULT -(1L << 31)

// -------------------- LCD Setup --------------------
#define I2C_LCD_ADDRESS 0x27 // Change to 0x3F if needed
LiquidCrystal_I2C mainDisplay(I2C_LCD_ADDRESS, 16, 2);

// -------------------- Button Pins --------------------
#define BUTTON_0        2
#define BUTTON_1        3
#define BUTTON_OPEN     4
#define BUTTON_CLOSE    5
#define BUTTON_PLUS     6
#define BUTTON_MINUS    7
#define BUTTON_MULTIPLY 8
#define BUTTON_DIVIDE    9
#define BUTTON_EQUAL    10
#define BUTTON_BACK     11

// -------------------- Global Variables --------------------
String userInput = "";

const int pins[10] = {
  BUTTON_0, BUTTON_1, BUTTON_OPEN, BUTTON_CLOSE, BUTTON_PLUS,
  BUTTON_MINUS, BUTTON_MULTIPLY, BUTTON_DIVIDE, BUTTON_EQUAL, BUTTON_BACK
};

const char buttonSymbols[10] = {
  '0', '1', '(', ')', '+', '-', '*', '/', '=', 'B'
};

// -------------------- Function Prototypes --------------------
long evaluateExpression(const char* expression);
void handleButton(char button);
void updateDisplay(String line1, String line2);
String convertBCD(String binaryInput);
String convertToBinary(String decimalInput);
bool isValidExpression(String expression);

// =======================================================================
//  1) Define 'TokenNode' to store tokens (binary or operator/parentheses)
// =======================================================================
struct TokenNode {
  String tokenValue; // e.g. "101", "+", "("
  bool isBinary;     // true if it's a binary number, false if operator/paren
};

// -----------------------------------------------------------------------
//  2) Debounce logic: detect button press transitions (HIGH->LOW)
// -----------------------------------------------------------------------
bool isButtonPressed(int pin) {
  static bool previousStates[12] = {false};
  bool currentState = (digitalRead(pin) == LOW); // Active-low
  bool pressed = (currentState && !previousStates[pin]);
  previousStates[pin] = currentState;
  return pressed;
}

// -----------------------------------------------------------------------
//  3) Convert a binary string "101" -> decimal 5
// -----------------------------------------------------------------------
long bin2Dec(const String &binaryStr) {
  long value = 0;
  for (size_t i = 0; i < binaryStr.length(); i++) {
    char c = binaryStr[i];
    if (c == '0') {
      value <<= 1;
    } else if (c == '1') {
      value = (value << 1) | 1;
    } else {
      // Invalid character for binary
      return -1;
    }
  }
  return value;
}

// -----------------------------------------------------------------------
//  4) Tokenizing the expression into TokenNodes
// -----------------------------------------------------------------------
#define TOKEN_CAP 100

int lexify(const String &expr, TokenNode tokenNodes[]) {
  int tokCount = 0;
  int i = 0;
  while (i < (int)expr.length()) {
    if (tokCount >= TOKEN_CAP) break; // no more space

    char ch = expr[i];
    if (ch == '0' || ch == '1') {
      // accumulate a binary number
      String number;
      while (i < (int)expr.length() && (expr[i] == '0' || expr[i] == '1')) {
        number += expr[i];
        i++;
      }
      tokenNodes[tokCount].tokenValue = number;
      tokenNodes[tokCount].isBinary   = true;
      tokCount++;
    }
    else if (ch == '(' || ch == ')' || ch == '+' || ch == '-' || ch == '*' || ch == '/') {
      tokenNodes[tokCount].tokenValue = String(ch);
      tokenNodes[tokCount].isBinary   = false;
      tokCount++;
      i++;
    }
    else {
      // skip other chars
      i++;
    }
  }
  return tokCount;
}

// -----------------------------------------------------------------------
//  5) Evaluate tokens with a shunting-yard style parser
// -----------------------------------------------------------------------
int getOperatorPrecedence(char op) {
  if (op == '+' || op == '-') return 1;
  if (op == '*' || op == '/') return 2;
  return 0;
}

long executeOperation(long a, long b, char op) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/':
      return a / b;
  }
  return BAD_RESULT; // Invalid operation
}

long interpretTokens(TokenNode tokens[], int count) {
  ArduinoQueue<long> outputQueue(64);
  SimpleStack<char> operatorStack(64);

  // Shunting-Yard Algorithm to convert to Reverse Polish Notation (RPN)
  for (int i = 0; i < count; i++) {
    if (tokens[i].isBinary) {
      long number = bin2Dec(tokens[i].tokenValue);
      outputQueue.enqueue(number);
    } else {
      char currentOp = tokens[i].tokenValue[0];
      if (currentOp == '(') {
        operatorStack.push(currentOp);
      }
      else if (currentOp == ')') {
        char topOp;
        while (operatorStack.peek(&topOp) && topOp != '(') {
          operatorStack.pop(&topOp);
          outputQueue.enqueue(static_cast<long>(topOp) + (1L << 30));
        }
        operatorStack.pop(&topOp); // Remove '('
      }
      else {
        // currentOp is +, -, *, or /
        char topOp; // Declare topOp before using it
        while (!operatorStack.isEmpty() && operatorStack.peek(&topOp) && topOp != '(' &&
               getOperatorPrecedence(topOp) >= getOperatorPrecedence(currentOp)) {
          operatorStack.pop(&topOp);
          outputQueue.enqueue(static_cast<long>(topOp) + (1L << 30));
        }
        operatorStack.push(currentOp);
      }
    }
  }

  // Pop any remaining operators to the output queue
  while (!operatorStack.isEmpty()) {
    char poppedOp;
    operatorStack.pop(&poppedOp);
    outputQueue.enqueue(static_cast<long>(poppedOp) + (1L << 30));
  }

  // Evaluate the RPN expression
  SimpleStack<long> evaluationStack(64);
  while (!outputQueue.isEmpty()) {
    long token = outputQueue.getHead();
    if (token > (1L << 30)) {
      // Operator token
      char op = static_cast<char>(token - (1L << 30));
      long operand2, operand1;
      if (!evaluationStack.pop(&operand2) || !evaluationStack.pop(&operand1)) {
        return BAD_RESULT; // Invalid expression
      }
      long result = executeOperation(operand1, operand2, op);
      if (result == BAD_RESULT) return BAD_RESULT; // Division by zero or invalid op
      evaluationStack.push(result);
      outputQueue.dequeue();
    }
    else {
      // Operand token
      evaluationStack.push(outputQueue.dequeue());
    }
  }

  // Final result should be the only item on the stack
  long finalResult;
  if (!evaluationStack.pop(&finalResult) || !evaluationStack.isEmpty()) {
    return BAD_RESULT; // Invalid expression
  }
  return finalResult;
}

// -----------------------------------------------------------------------
//  6) Parse and Evaluate the Expression
// -----------------------------------------------------------------------
long parseAndEvaluate(const String &expr) {
  if (expr.length() == 0) return BAD_RESULT; // Empty expression

  TokenNode tokens[TOKEN_CAP];
  int tokenCount = lexify(expr, tokens);
  if (tokenCount == 0) return BAD_RESULT; // No valid tokens

  return interpretTokens(tokens, tokenCount);
}

// -----------------------------------------------------------------------
//  7) Display the Expression in Decimal
// -----------------------------------------------------------------------
String displayInDecimal(const String &expr) {
  TokenNode tokens[TOKEN_CAP];
  int tokenCount = lexify(expr, tokens);

  String output = "";
  for (int i = 0; i < tokenCount; i++) {
    if (tokens[i].isBinary) {
      long decimalValue = bin2Dec(tokens[i].tokenValue);
      if (decimalValue >= 0)
        output += String(decimalValue);
      else
        output += "?"; // Invalid binary number
    }
    else {
      output += tokens[i].tokenValue;
    }
    output += " ";
  }
  return output;
}


// -----------------------------------------------------------------------
//  8) Handle Button Presses
// -----------------------------------------------------------------------
void handleButtonPress(char button) {
  if (button == 'B') { // Backspace
    if (userInput.length() > 0)
      
      userInput.remove(userInput.length() - 1);
  }
  else if (button == 'C') { // Clear (if implemented)
    userInput = "";
  }
  else if (button == '(') {
    if (userInput.length() > 0) {
      char lastChar = userInput[userInput.length() - 1];
      if (lastChar == '0' || lastChar == '1')
        userInput += '*';
    }
    userInput += button;
  }
  else if (button == '=') { // Evaluate Expression
    String resultStr = "";
    long result = parseAndEvaluate(userInput);
    mainDisplay.clear();
    if (result == BAD_RESULT) {
      Serial.println("RESULT:ERROR");
      mainDisplay.print("INVALID INPUT!");
    } else {
      Serial.print("RESULT:");
      Serial.println(result);
      mainDisplay.print(result);
    }
    delay(1500);
    // Clear expression
    userInput = "";
    mainDisplay.clear();
    return;
  }
  else {
    userInput += button;
  }
  updateDisplay(convertBCD(userInput));
}

// -----------------------------------------------------------------------
//  9) Update LCD Display
// -----------------------------------------------------------------------
void updateDisplay(String line) {
  Serial.println("EXPR:" + line);
  mainDisplay.clear();
  mainDisplay.setCursor(0, 0);
  mainDisplay.print(line);
}

// -----------------------------------------------------------------------
// 10) Convert Binary Coded Decimal to Decimal Representation
// -----------------------------------------------------------------------
String convertBCD(String binaryInput) {
  String converted = "";
  String temp = "";
  for (char c : binaryInput) {
    if (c == '0' || c == '1')
      temp += c;
    else {
      if (temp.length() > 0) {
        converted += String(strtol(temp.c_str(), NULL, 2));
        temp = "";
      }
      converted += c;
    }
  }
  if (temp.length() > 0)
    converted += String(strtol(temp.c_str(), NULL, 2));
  return converted;
}

// -----------------------------------------------------------------------
// 11) Convert Decimal String to Binary Representation
// -----------------------------------------------------------------------
String convertToBinary(String decimalInput) {
  String binaryStr = "";
  String temp = "";
  for (char c : decimalInput) {
    if (isdigit(c))
      temp += c;
    else {
      if (temp.length() > 0) {
        binaryStr += String(strtol(temp.c_str(), NULL, 10), BIN);
        temp = "";
      }
      binaryStr += c;
    }
  }
  if (temp.length() > 0)
    binaryStr += String(strtol(temp.c_str(), NULL, 10), BIN);
  return binaryStr;
}

// -----------------------------------------------------------------------
// 12) Validate the Expression Syntax
// -----------------------------------------------------------------------
bool isValidExpression(String expr) {
  if (expr.length() == 0) return false;
  
  // Check for invalid starting characters
  char firstChar = expr[0];
  if (firstChar == '+' || firstChar == '*' || firstChar == '/')
    return false;
  
  // Check for invalid ending characters
  char lastChar = expr[expr.length() - 1];
  if (lastChar == '+' || lastChar == '-' || lastChar == '*' || lastChar == '/')
    return false;

  int openBrackets = 0;
  for (int i = 0; i < expr.length(); i++) {
    char current = expr[i];
    if (current == '(') openBrackets++;
    if (current == ')') openBrackets--;
    if (openBrackets < 0) return false; // More closing brackets

    if (i > 0) {
      char previous = expr[i - 1];
      // Check for consecutive operators
      if ((previous == '+' || previous == '-' || previous == '*' || previous == '/') &&
          (current == '+' || current == '-' || current == '*' || current == '/'))
        return false;
      // Check for invalid transitions
      if ((previous == '0' || previous == '1' || previous == ')') && current == '(')
        return false;
      if (previous == ')' && (current == '0' || current == '1' || current == '('))
        return false;
      if ((current == '*' || current == '/') && previous == '(')
        return false;
    }
  }
  return openBrackets == 0;
}

// -----------------------------------------------------------------------
// 13) Arduino Setup
// -----------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Initialize button pins
  for (int i = 0; i < 10; i++) {
    pinMode(pins[i], INPUT_PULLUP);
  }

  // Initialize LCD
  Wire.begin();
  mainDisplay.init();
  mainDisplay.backlight();
  mainDisplay.clear();
  mainDisplay.setCursor(0, 0);
  mainDisplay.print("HELLO!");
  delay(1200);
  mainDisplay.clear();
}

// -----------------------------------------------------------------------
// 14) Arduino Loop
// -----------------------------------------------------------------------
void loop() {
  // Check for serial input
  if (Serial.available() > 0) {
    char serialInput = Serial.read();
    if (serialInput != 0)
      handleButtonPress(serialInput);
  }

  // Check each button for presses
  for (int i = 0; i < 10; i++) {
    if (isButtonPressed(pins[i])) {
      char pressedSymbol = buttonSymbols[i];
      handleButtonPress(pressedSymbol);
      delay(300); 
    }
  }
}
