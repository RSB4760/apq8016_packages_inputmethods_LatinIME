/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LATINIME_TYPING_WEIGHTING_H
#define LATINIME_TYPING_WEIGHTING_H

#include "defines.h"
#include "suggest_utils.h"
#include "suggest/core/dicnode/dic_node_utils.h"
#include "suggest/core/policy/weighting.h"
#include "suggest/core/session/dic_traverse_session.h"
#include "suggest/policyimpl/typing/scoring_params.h"

namespace latinime {

class DicNode;
struct DicNode_InputStateG;

class TypingWeighting : public Weighting {
 public:
    static const TypingWeighting *getInstance() { return &sInstance; }

 protected:
    float getTerminalSpatialCost(
            const DicTraverseSession *const traverseSession, const DicNode *const dicNode) const {
        float cost = 0.0f;
        if (dicNode->hasMultipleWords()) {
            cost += ScoringParams::HAS_MULTI_WORD_TERMINAL_COST;
        }
        if (dicNode->getProximityCorrectionCount() > 0) {
            cost += ScoringParams::HAS_PROXIMITY_TERMINAL_COST;
        }
        if (dicNode->getEditCorrectionCount() > 0) {
            cost += ScoringParams::HAS_EDIT_CORRECTION_TERMINAL_COST;
        }
        return cost;
    }

    float getOmissionCost(const DicNode *const parentDicNode, const DicNode *const dicNode) const {
        bool sameCodePoint = false;
        bool isFirstLetterOmission = false;
        float cost = 0.0f;
        sameCodePoint = dicNode->isSameNodeCodePoint(parentDicNode);
        // If the traversal omitted the first letter then the dicNode should now be on the second.
        isFirstLetterOmission = dicNode->getDepth() == 2;
        if (isFirstLetterOmission) {
            cost = ScoringParams::OMISSION_COST_FIRST_CHAR;
        } else {
            cost = sameCodePoint ? ScoringParams::OMISSION_COST_SAME_CHAR
                    : ScoringParams::OMISSION_COST;
        }
        return cost;
    }

    float getMatchedCost(
            const DicTraverseSession *const traverseSession, const DicNode *const dicNode,
            DicNode_InputStateG *inputStateG) const {
        const int pointIndex = dicNode->getInputIndex(0);
        // Note: min() required since length can be MAX_POINT_TO_KEY_LENGTH for characters not on
        // the keyboard (like accented letters)
        const float normalizedSquaredLength = traverseSession->getProximityInfoState(0)
                ->getPointToKeyLength(pointIndex, dicNode->getNodeCodePoint());
        const float normalizedDistance = SuggestUtils::getSweetSpotFactor(
                traverseSession->isTouchPositionCorrectionEnabled(), normalizedSquaredLength);
        const float weightedDistance = ScoringParams::DISTANCE_WEIGHT_LENGTH * normalizedDistance;

        const bool isFirstChar = pointIndex == 0;
        const bool isProximity = isProximityDicNode(traverseSession, dicNode);
        const float cost = isProximity ? (isFirstChar ? ScoringParams::FIRST_PROXIMITY_COST
                : ScoringParams::PROXIMITY_COST) : 0.0f;
        return weightedDistance + cost;
    }

    bool isProximityDicNode(
            const DicTraverseSession *const traverseSession, const DicNode *const dicNode) const {
        const int pointIndex = dicNode->getInputIndex(0);
        const int primaryCodePoint = toBaseLowerCase(
                traverseSession->getProximityInfoState(0)->getPrimaryCodePointAt(pointIndex));
        const int dicNodeChar = toBaseLowerCase(dicNode->getNodeCodePoint());
        return primaryCodePoint != dicNodeChar;
    }

    float getTranspositionCost(
            const DicTraverseSession *const traverseSession, const DicNode *const parentDicNode,
            const DicNode *const dicNode) const {
        const int16_t parentPointIndex = parentDicNode->getInputIndex(0);
        const int prevCodePoint = parentDicNode->getNodeCodePoint();
        const float distance1 = traverseSession->getProximityInfoState(0)->getPointToKeyLength(
                parentPointIndex + 1, prevCodePoint);
        const int codePoint = dicNode->getNodeCodePoint();
        const float distance2 = traverseSession->getProximityInfoState(0)->getPointToKeyLength(
                parentPointIndex, codePoint);
        const float distance = distance1 + distance2;
        const float weightedLengthDistance =
                distance * ScoringParams::DISTANCE_WEIGHT_LENGTH;
        return ScoringParams::TRANSPOSITION_COST + weightedLengthDistance;
    }

    float getInsertionCost(
            const DicTraverseSession *const traverseSession,
            const DicNode *const parentDicNode, const DicNode *const dicNode) const {
        const int16_t parentPointIndex = parentDicNode->getInputIndex(0);
        const int prevCodePoint =
                traverseSession->getProximityInfoState(0)->getPrimaryCodePointAt(parentPointIndex);

        const int currentCodePoint = dicNode->getNodeCodePoint();
        const bool sameCodePoint = prevCodePoint == currentCodePoint;
        const float dist = traverseSession->getProximityInfoState(0)->getPointToKeyLength(
                parentPointIndex + 1, currentCodePoint);
        const float weightedDistance = dist * ScoringParams::DISTANCE_WEIGHT_LENGTH;
        const bool singleChar = dicNode->getDepth() == 1;
        const float cost = (singleChar ? ScoringParams::INSERTION_COST_FIRST_CHAR : 0.0f)
                + (sameCodePoint ? ScoringParams::INSERTION_COST_SAME_CHAR
                        : ScoringParams::INSERTION_COST);
        return cost + weightedDistance;
    }

    float getNewWordCost(const DicTraverseSession *const traverseSession,
            const DicNode *const dicNode) const {
        const bool isCapitalized = dicNode->isCapitalized();
        const float cost = isCapitalized ?
                ScoringParams::COST_NEW_WORD_CAPITALIZED : ScoringParams::COST_NEW_WORD;
        return cost * traverseSession->getMultiWordCostMultiplier();
    }

    float getNewWordBigramCost(
            const DicTraverseSession *const traverseSession, const DicNode *const dicNode,
            hash_map_compat<int, int16_t> *const bigramCacheMap) const {
        return DicNodeUtils::getBigramNodeImprobability(traverseSession->getOffsetDict(),
                dicNode, bigramCacheMap);
    }

    float getCompletionCost(const DicTraverseSession *const traverseSession,
            const DicNode *const dicNode) const {
        // The auto completion starts when the input index is same as the input size
        const bool firstCompletion = dicNode->getInputIndex(0)
                == traverseSession->getInputSize();
        // TODO: Change the cost for the first completion for the gesture?
        const float cost = firstCompletion ? ScoringParams::COST_FIRST_LOOKAHEAD
                : ScoringParams::COST_LOOKAHEAD;
        return cost;
    }

    float getTerminalLanguageCost(const DicTraverseSession *const traverseSession,
            const DicNode *const dicNode, const float dicNodeLanguageImprobability) const {
        const bool hasEditCount = dicNode->getEditCorrectionCount() > 0;
        const bool isSameLength = dicNode->getDepth() == traverseSession->getInputSize();
        const bool hasMultipleWords = dicNode->hasMultipleWords();
        const bool hasProximityErrors = dicNode->getProximityCorrectionCount() > 0;
        // Gesture input is always assumed to have proximity errors
        // because the input word shouldn't be treated as perfect
        const bool isExactMatch = !hasEditCount && !hasMultipleWords
                && !hasProximityErrors && isSameLength;

        const float totalPrevWordsLanguageCost = dicNode->getTotalPrevWordsLanguageCost();
        const float languageImprobability = isExactMatch ? 0.0f : dicNodeLanguageImprobability;
        const float languageWeight = ScoringParams::DISTANCE_WEIGHT_LANGUAGE;
        // TODO: Caveat: The following equation should be:
        // totalPrevWordsLanguageCost + (languageImprobability * languageWeight);
        return (totalPrevWordsLanguageCost + languageImprobability) * languageWeight;
    }

    AK_FORCE_INLINE bool needsToNormalizeCompoundDistance() const {
        return false;
    }

    AK_FORCE_INLINE float getAdditionalProximityCost() const {
        return ScoringParams::ADDITIONAL_PROXIMITY_COST;
    }

    AK_FORCE_INLINE float getSubstitutionCost() const {
        return ScoringParams::SUBSTITUTION_COST;
    }

    AK_FORCE_INLINE float getSpaceSubstitutionCost(
            const DicTraverseSession *const traverseSession,
            const DicNode *const dicNode) const {
        const bool isCapitalized = dicNode->isCapitalized();
        const float cost = ScoringParams::SPACE_SUBSTITUTION_COST + (isCapitalized ?
                ScoringParams::COST_NEW_WORD_CAPITALIZED : ScoringParams::COST_NEW_WORD);
        return cost * traverseSession->getMultiWordCostMultiplier();
    }

 private:
    DISALLOW_COPY_AND_ASSIGN(TypingWeighting);
    static const TypingWeighting sInstance;

    TypingWeighting() {}
    ~TypingWeighting() {}
};
} // namespace latinime
#endif // LATINIME_TYPING_WEIGHTING_H
