let addStringCodeFragments = (function(){
    const replacement = function(c) {
        return c === '\n' ? '\\n'
             : c === '\t' ? '\\t'
             : '\\' + c
    };

    const pushJSString = function(codeFragments, str) {
        // when no quote or single and double quote, prefer single quote
        if (str.indexOf("'") === -1 || str.indexOf('"') !== -1) {
            codeFragments.push("'");
            codeFragments.push(str.replace(/[\n\t\'\\]/g, replacement));
            codeFragments.push("'");
        }
        else {
            codeFragments.push('"');
            codeFragments.push(str.replace(/[\n\t\"\\]/g, replacement));
            codeFragments.push('"');
        }
    };

    return function(value, codeFragments, ancestors, sortedKey) {
        const t = typeof value;
        if (t === 'string')
            pushJSString(codeFragments, value);
        else if (value === null)
            codeFragments.push('null');
        else if (value === undefined)
            codeFragments.push('undefined');
        else if (t !== 'object')
            codeFragments.push(value);
        else if (ancestors.includes(value))
            codeFragments.push("[Circular]");
        // assume array or array like
        else if (value[Symbol.iterator]) {
            ancestors.push(value);

            const isArray = Array.isArray(value);
            if (isArray)
                codeFragments.push('[');
            else {
                const name = value.constructor.name;
                codeFragments.push('new ');
                codeFragments.push(value.constructor.name);
                codeFragments.push('([');
            }

            const codeFragmentsLen = codeFragments.length;
            for (const o of value) {
                addStringCodeFragments(o, codeFragments, ancestors, sortedKey);
                codeFragments.push(', ');
            }
            // remove last ', '
            if (codeFragmentsLen !== codeFragments.length)
                codeFragments.pop();
            codeFragments.push(isArray ? ']' : '])');

            ancestors.pop();
        }
        // is object
        else {
            ancestors.push(value);

            codeFragments.push('{');
            const codeFragmentsLen = codeFragments.length;
            const keys = Object.keys(value);
            for (const key of sortedKey ? keys.sort() : keys) {
                if (/^[a-zA-Z][\w\d]*/.test(key))
                    codeFragments.push(key);
                else
                    pushJSString(codeFragments, key);
                codeFragments.push(': ');
                addStringCodeFragments(value[key], codeFragments, ancestors, sortedKey);
                codeFragments.push(', ');
            }
            // remove last ', '
            if (codeFragmentsLen !== codeFragments.length)
                codeFragments.pop();
            codeFragments.push('}');

            ancestors.pop();
        }
    };
})();

function toComparableStringCode(value) {
    const codeFragments = [];
    addStringCodeFragments(value, codeFragments, [], true);
    return codeFragments.join('');
}

function jsCmdToStringCode(fn, args) {
    // the Fonction object has a name, but may be empty
    const code = (typeof fn === 'function') ? (fn.name || '/*Unamed Function*/')
        : (fn === undefined) ? 'undefined'
        : (fn === null) ? 'null'
        : fn.toString();

    if (args.length === 0) {
        return code + '()';
    }

    const codeFragments = [code, '('];
    for (const arg of args) {
        addStringCodeFragments(arg, codeFragments, []);
        codeFragments.push(', ');
    }
    codeFragments[codeFragments.length - 1] = ')';
    return codeFragments.join('');
}

function normalizeConfig(config) {
    if (config.enablePlaceholders !== undefined) {
        const b = (config.enablePlaceholders === true);
        delete config.enablePlaceholders;
        m_config.enableCursorPlaceholder = b;
        m_config.enableVirtualCursorPlaceholder = b;
        m_config.enableSelectionPlaceholder = b;
    }
}

const OUTPUT_IS_OK = (1 << 0);
const CONTAINS_RESULT_OR_ERROR = 1 << 1;
const EXPECTED_ERROR_BUT_NO_ERROR = (1 << 2) | CONTAINS_RESULT_OR_ERROR;
const EXPECTED_NO_ERROR_BUT_ERROR = (1 << 3);
const IS_RESULT_NOT_ERROR = (1 << 4) | CONTAINS_RESULT_OR_ERROR;
const IS_ERROR_NOT_RESULT = CONTAINS_RESULT_OR_ERROR;
const SAME_RESULT_OR_ERROR = (1 << 5) | CONTAINS_RESULT_OR_ERROR;

///\return {result, expectedResult, options}
function compareResults(internal, result, expectedResult, exception) {
    if (expectedResult) {
        if (expectedResult.error) {
            if (expectedResult.error === true) {
                if (!exception) {
                    return {
                        result: '',
                        expectedResult: '',
                        options: EXPECTED_ERROR_BUT_NO_ERROR,
                    };
                }
            }
            else {
                const err = exception.toString();
                if (expectedResult.error !== err) {
                    return {
                        result: err,
                        expectedResult: expectedResult.error,
                        options: IS_ERROR_NOT_RESULT,
                    };
                }
            }
        }
        else if ('result' in expectedResult) {
            result = toComparableStringCode(result);
            expectedResult = toComparableStringCode(expectedResult.result);
            let options = (exception ? EXPECTED_NO_ERROR_BUT_ERROR : 0);
            if (result !== expectedResult) {
                options |= IS_RESULT_NOT_ERROR;
            }
            return {result, expectedResult, options};
        }
    }
    return {result: '', expectedResult: '', options: (exception ? EXPECTED_NO_ERROR_BUT_ERROR : 0)};
}

export const DUAL_MODE = 2;

const defaultConfig = {
    syntax: "None",
    indentationMode: "none",
    // encoding: undefined,
    blockSelection: DUAL_MODE,
    indentationWidth: 4,
    tabWidth: 4,
    replaceTabs: false,
    overrideMode: false,
    indentPastedTest: false,
};

class TestCase {
    constructor(name, internal, env, config) {
        this.name = name;
        this.internal = internal;
        this.env = env;
        this.config = Object.assign(config, defaultConfig);
        normalizeConfig(config);
    }

    setConfig(config) {
        Object.assign(this.config, config);
        normalizeConfig(this.config);
        return this;
    }

    /// @param msgOrOptions: String | {[message: String, result: Any, error: String | bool]}
    cmd(command, input, expectedOutput, msgOrOptions) {
        const optionalDesc = msgOrOptions
            ? ((typeof msgOrOptions === 'string') ? msgOrOptions : msgOrOptions.message)
            : undefined;
        const expectedResult = (typeof msgOrOptions === 'object') ? msgOrOptions : undefined;
        // TODO use optionalDesc
        this.internal.setConfig(this.config);

        // TODO use this.name

        // TODO find cursor / selection

        // TODO replace cursor
        this.internal.setInput(input);

        // TODO blockSelection mode

        if (typeof command === 'function') {
            let exception;
            let result;
            try {
                result = command();
            } catch (e) {
                exception = e;
            }

            // TODO code duplicate
            const isSame = this.internal.checkOutput(expectedOutput);
            const resultComparison = compareResults(this.internal, result, expectedResult, exception);
            if (!isSame || resultComparison.options) {
                const code = command.name || '/*Unamed Function*/'; // TODO no code duplicate
                if (isSame) {
                    resultComparison.options |= OUTPUT_IS_OK;
                }
                this.internal.cmdDiffer(1, code, exception, resultComparison.result, resultComparison.expectedResult, resultComparison.options);
            }
        }
        else if (typeof command === 'string') {
            const code = command.toString();
            let exception;
            let result;
            try {
                result = this.internal.evaluate(code);
            } catch (e) {
                exception = e;
            }

            // TODO code duplicate
            const isSame = this.internal.checkOutput(expectedOutput);
            const resultComparison = compareResults(this.internal, result, expectedResult, exception);
            if (!isSame || resultComparison.options) {
                if (isSame) {
                    resultComparison.options |= OUTPUT_IS_OK;
                }
                this.internal.cmdDiffer(1, code, exception, resultComparison.result, resultComparison.expectedResult, resultComparison.options);
            }
        }
        else if (Array.isArray(command)) {
            const args = command.slice(1);
            let fn = command[0];

            /**
             * Transform string to function.
             * Useful with the form `obj.func` which requires that the parameter
             * be `obj.func.bind(obj)` or `(...arg) => obj.func(...arg)` so as not
             * to lose this which makes it impossible to display the function used
             * in the test message.
             */
            if (typeof fn === 'string') {
                fn = this.internal.evaluate(`(function(){ return ${fn}(...arguments); })`)
            }

            let exception;
            let result;
            try {
                result = fn.apply(undefined, args);
            } catch (e) {
                exception = e;
            }

            // TODO code duplicate
            const isSame = this.internal.checkOutput(expectedOutput);
            const resultComparison = compareResults(this.internal, result, expectedResult, exception);
            if (!isSame || resultComparison.options) {
                const code = jsCmdToStringCode(command[0], args); // TODO no code duplicate
                if (isSame) {
                    resultComparison.options |= OUTPUT_IS_OK;
                }
                this.internal.cmdDiffer(1, code, exception, resultComparison.result, resultComparison.expectedResult, resultComparison.options);
            }
        }

        return this;
    }
};

export class TestFramework {
    constructor(internal, env) {
        this.internal = internal;
        this.env = env;
        this.config = {};
    }

    // TODO
    // load(mod)

    setConfig(config) {
        Object.assign(this.config, config);
        return this;
    }

    testCase(nameOrFn, fn) {
        if (!fn) {
            fn = nameOrFn;
            nameOrFn = undefined;
        }
        const testCase = new TestCase(nameOrFn, this.internal, this.env, this.config);
        fn.call(testCase, testCase, this.env);
        return this;
    }
}

export function calleeWrapper(name, obj, newObj) {
    newObj = newObj || {};
    for (const key of Object.keys(obj)) {
        Object.defineProperty(newObj, key, {
            get: () => {
                let o = obj[key];
                if (typeof o === 'function') {
                    const funcName = `${name}.${key}`;
                    // create a function with funcName as function name (property Function.name)
                    o = {[funcName]: (...args) => obj[key](...args)}[funcName];
                }
                return o;
            },
            set: (value) => obj[key] = value,
        });
    }
    return newObj;
}
