#ifndef INDEX_POLICY_H
#define INDEX_POLICY_H

/**
 * Index policy: ONLY primary index, NO composite, NO secondary.
 * This policy is fixed and cannot be changed.
 * - Exactly one column must be PRIMARY KEY
 * - UNIQUE (secondary index) is not allowed
 * - Composite primary key (multiple PK columns) is not allowed
 */
namespace IndexPolicy {
    constexpr bool ALLOW_ONLY_PRIMARY_INDEX = true;
    constexpr bool ALLOW_COMPOSITE_PRIMARY_KEY = false;
    constexpr bool ALLOW_UNIQUE_SECONDARY_INDEX = false;
}

#endif
