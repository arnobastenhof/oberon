/*
 * MinUnit is a three-line test framework for C proposed by Jera Design
 * (http://www.jera.com/techinfo/jtns/jtn002.html). The current header extends
 * these efforts with separate counts for the successful and failed test cases,
 * as well as a number of specialized asserts inspired mostly by JUnit.
 */

#ifndef MINUNIT_H_
#define MINUNIT_H_

/* General-purpose assertion macro. */
#define ASSERT(msg, cond) do {    \
    if (!(cond)) {                \
        return (msg);             \
    }                             \
} while (0)

/* Succeeds iff cond != 0. */
#define ASSERT_TRUE(cond) \
ASSERT("Expected true: " #cond, (cond))

/* Succeeds iff cond == 0. */
#define ASSERT_FALSE(cond) \
ASSERT("Expected false: " #cond, !(cond))

/* Succeeds iff obj != NULL. */
#define ASSERT_NOT_NULL(obj) \
ASSERT("Expected not NULL: " #obj, (obj))

/* Succeeds iff obj == NULL. */
#define ASSERT_NULL(obj) \
ASSERT("Expected NULL: " #obj, !(obj))

/* Succeeds iff exp == act. */
#define ASSERT_EQ(exp, act) \
ASSERT("Expected equal: " #exp ", " #act, (exp) == (act))

/* Succeeds iff cmp(exp, act) == 0. */
#define ASSERT_EQ_CMP(cmp, exp, act) \
ASSERT("Expected equal: " #exp ", " #act, (cmp)((exp), (act)) == 0)

/* Fails the test. */
#define FAIL() \
return ""

#endif /* MINUNIT_H_ */
