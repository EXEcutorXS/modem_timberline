#ifndef OPERATOR_NAMES_H
#define OPERATOR_NAMES_H

/* Look up a carrier name by numeric MCC+MNC code, e.g. "25099" -> "Beeline".
   Returns NULL if the code isn't in the table. */
const char* findOperatorName(const char* mccMnc);

#endif /* OPERATOR_NAMES_H */
