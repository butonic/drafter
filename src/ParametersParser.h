//
//  ParametersParser.h
//  snowcrash
//
//  Created by Zdenek Nemec on 9/1/13.
//  Copyright (c) 2013 Apiary Inc. All rights reserved.
//

#ifndef SNOWCRASH_PARAMETERSPARSER_H
#define SNOWCRASH_PARAMETERSPARSER_H

#include <sstream>
#include "BlueprintParserCore.h"
#include "Blueprint.h"
#include "ListUtility.h"
#include "RegexMatch.h"
#include "StringUtility.h"
#include "ParameterDefinitonParser.h"

/** Parameters matching regex */
static const std::string ParametersRegex("^[ \\t]*[Pp]arameters?[ \\t]*$");

/** Expected parameters content */
static const std::string ExpectedParametersContent = "a nested list of parameters, one parameter per list item";

/** No parameters specified message */
static const std::string NoParametersMessage = "no parameters specified, expected a nested list of parameters, one parameter per list item";

namespace snowcrash {
    
    /** Internal type alias for Collection of Paramaeter */
    typedef Collection<Parameter>::type ParameterCollection;

    
    /** Finds a parameter inside a parameters collection */
    FORCEINLINE ParameterCollection::iterator FindParameter(ParameterCollection& parameters,
                                                           const Parameter& parameter) {
        return std::find_if(parameters.begin(),
                            parameters.end(),
                            std::bind2nd(MatchName<Parameter>(), parameter));
    }
    
    /**
     *  Returns true if given block has parameters signature, false otherwise.
     */
    FORCEINLINE bool HasParametersSignature(const BlockIterator& begin,
                                            const BlockIterator& end) {

        if (begin->type != ListBlockBeginType &&
            begin->type != ListItemBlockBeginType)
            return false;
        
        SourceData remainingContent;
        SourceData content = GetListItemSignature(begin, end, remainingContent);
        TrimString(content);
        return RegexMatch(content, ParametersRegex);
    }
    
    /**
     *  Block Classifier, ParameterCollection context.
     */
    template <>
    FORCEINLINE SectionType ClassifyBlock<ParameterCollection>(const BlockIterator& begin,
                                                           const BlockIterator& end,
                                                           const SectionType& context) {
        
        if (context == UndefinedSectionType) {
            if (HasParametersSignature(begin, end))
                return ParametersSectionType;
        }
        else if (context == ParametersSectionType) {
            
            if (begin->type == ListItemBlockEndType ||
                begin->type == ListBlockEndType)
                return UndefinedSectionType;
            
            if (HasParameterDefinitionSignature(begin, end))
                return ParameterDefinitionSectionType;
            
            if (begin->type == ListBlockBeginType)
                return ForeignSectionType; // Foreign nested list-item
            
            if (begin->type == ListItemBlockBeginType)
                return UndefinedSectionType;
        }
        else if (context == ParameterDefinitionSectionType ||
                 context == ForeignSectionType) {
            
            if (begin->type == ListItemBlockEndType ||
                begin->type == ListBlockEndType)
                return UndefinedSectionType;
            
            if (HasParameterDefinitionSignature(begin, end))
                return ParameterDefinitionSectionType;
            
            return ForeignSectionType;
        }
        
        return (context == ParametersSectionType) ? context : UndefinedSectionType;
    }

    /**
     *  Parameters section parser.
     */
    template<>
    struct SectionParser<ParameterCollection> {
        
        static ParseSectionResult ParseSection(const SectionType& section,
                                               const BlockIterator& cur,
                                               const SectionBounds& bounds,
                                               BlueprintParserCore& parser,
                                               ParameterCollection& parameters) {
            
            ParseSectionResult result = std::make_pair(Result(), cur);
            switch (section) {
                case ParametersSectionType:
                    result = HandleParmetersSection(cur, bounds, parser, parameters);
                    break;
                    
                case ParameterDefinitionSectionType:
                    result = HandleParmeterDefinitionSection(cur, bounds, parser, parameters);
                    break;
                    
                case ForeignSectionType:
                    result = HandleForeignSection(cur, bounds, parser.sourceData, ExpectedParameterDefinition);
                    break;
                    
                case UndefinedSectionType:
                    result.second = CloseListItemBlock(cur, bounds.second);
                    break;
                    
                default:
                    result.first.error = UnexpectedBlockError(*cur, parser.sourceData);
                    break;
            }
            
            return result;
        }
        
        /**
         *  \brief  Parse Parameters top-level section blocks.
         *  \param  cur     The actual position within Markdown block buffer.
         *  \param  bounds  Boundaries of Markdown block buffer.
         *  \param  parser  Parser's instance.
         *  \param  payload An output buffer to write parameters into.
         *  \return A block parser section result.
         */
        static ParseSectionResult HandleParmetersSection(const BlockIterator& cur,
                                                         const SectionBounds& bounds,
                                                         BlueprintParserCore& parser,
                                                         ParameterCollection& parameters) {
            
            ParseSectionResult result = std::make_pair(Result(), cur);
            BlockIterator sectionCur = cur;
            
            // Signature
            if (sectionCur == bounds.first) {
                
                CheckSignatureAdditionalContent(sectionCur,
                                                bounds,
                                                parser.sourceData,
                                                "'parameters' keyword",
                                                ExpectedParametersContent,
                                                result.first);
                result.second = SkipSignatureBlock(sectionCur, bounds.second);
                return result;
            }
            
            // Unexpected description
            if (sectionCur->type == QuoteBlockBeginType) {
                sectionCur = SkipToSectionEnd(sectionCur, bounds.second, QuoteBlockBeginType, QuoteBlockEndType);
            }
            else if (sectionCur->type == ListBlockBeginType) {
                sectionCur = SkipToSectionEnd(sectionCur, bounds.second, ListBlockBeginType, ListItemBlockEndType);
            }
            
            if (!CheckCursor(sectionCur, bounds, parser.sourceData, cur, result.first))
                return result;
            
            // WARN: on ignoring additional content
            std::stringstream ss;
            ss << "ignoring additional content in the 'parameters' definition, expected " << ExpectedParametersContent;
            
            SourceCharactersBlock sourceBlock = CharacterMapForBlock(sectionCur, bounds, cur, parser.sourceData);
            result.first.warnings.push_back(Warning(ss.str(),
                                                    IgnoringWarning,
                                                    sourceBlock));
            
            if (sectionCur != bounds.second)
                result.second = ++sectionCur;

            return result;
        }
        
        /** Parse a parameter definition top-level section blocks. */
        static ParseSectionResult HandleParmeterDefinitionSection(const BlockIterator& cur,
                                                                  const SectionBounds& bounds,
                                                                  BlueprintParserCore& parser,
                                                                  ParameterCollection& parameters) {
            Parameter parameter;
            ParseSectionResult result = ParameterDefinitionParser::Parse(cur, bounds.second, parser, parameter);
            if (result.first.error.code != Error::OK)
                return result;

            // Check duplicates
            if (!parameters.empty()) {
                ParameterCollection::iterator duplicate = FindParameter(parameters, parameter);
                if (duplicate != parameters.end()) {

                    // WARN: Parameter already defined
                    std::stringstream ss;
                    ss << "overshadowing previous parameter '" << parameter.name << "' definition";

                    BlockIterator nameBlock = ListItemNameBlock(cur, bounds.second);
                    SourceCharactersBlock sourceBlock = CharacterMapForBlock(nameBlock, bounds, cur, parser.sourceData);
                    result.first.warnings.push_back(Warning(ss.str(),
                                                            RedefinitionWarning,
                                                            sourceBlock));
                    
                    // Erase origan duplicate 
                    parameters.erase(duplicate);
                }
            }
            
            parameters.push_back(parameter);
            
            return result;
        }
    };
    
    typedef BlockParser<ParameterCollection, SectionParser<ParameterCollection> > ParametersParser;
}


#endif
