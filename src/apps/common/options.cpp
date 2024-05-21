/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * cam - Options parsing
 */

#include <assert.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <string.h>

#include "options.h"

/**
 * \enum OptionArgument
 * \brief Indicate if an option takes an argument
 *
 * \var OptionArgument::ArgumentNone
 * \brief The option doesn't accept any argument
 *
 * \var OptionArgument::ArgumentRequired
 * \brief The option requires an argument
 *
 * \var OptionArgument::ArgumentOptional
 * \brief The option accepts an optional argument
 */

/**
 * \enum OptionType
 * \brief The type of argument for an option
 *
 * \var OptionType::OptionNone
 * \brief No argument type, used for options that take no argument
 *
 * \var OptionType::OptionInteger
 * \brief Integer argument type, with an optional base prefix (`0` for base 8,
 * `0x` for base 16, none for base 10)
 *
 * \var OptionType::OptionString
 * \brief String argument
 *
 * \var OptionType::OptionKeyValue
 * \brief key=value list argument
 */

/* -----------------------------------------------------------------------------
 * Option
 */

/**
 * \struct Option
 * \brief Store metadata about an option
 *
 * \var Option::opt
 * \brief The option identifier
 *
 * \var Option::type
 * \brief The type of the option argument
 *
 * \var Option::name
 * \brief The option name
 *
 * \var Option::argument
 * \brief Whether the option accepts an optional argument, a mandatory
 * argument, or no argument at all
 *
 * \var Option::argumentName
 * \brief The argument name used in the help text
 *
 * \var Option::help
 * \brief The help text (may be a multi-line string)
 *
 * \var Option::keyValueParser
 * \brief For options of type OptionType::OptionKeyValue, the key-value parser
 * to parse the argument
 *
 * \var Option::isArray
 * \brief Whether the option can appear once or multiple times
 *
 * \var Option::parent
 * \brief The parent option
 *
 * \var Option::children
 * \brief List of child options, storing all options whose parent is this option
 *
 * \fn Option::hasShortOption()
 * \brief Tell if the option has a short option specifier (e.g. `-f`)
 * \return True if the option has a short option specifier, false otherwise
 *
 * \fn Option::hasLongOption()
 * \brief Tell if the option has a long option specifier (e.g. `--foo`)
 * \return True if the option has a long option specifier, false otherwise
 */
struct Option {
	int opt;
	OptionType type;
	const char *name;
	OptionArgument argument;
	const char *argumentName;
	const char *help;
	KeyValueParser *keyValueParser;
	bool isArray;
	Option *parent;
	std::list<Option> children;

	bool hasShortOption() const { return isalnum(opt); }
	bool hasLongOption() const { return name != nullptr; }
	const char *typeName() const;
	std::string optionName() const;
};

/**
 * \brief Retrieve a string describing the option type
 * \return A string describing the option type
 */
const char *Option::typeName() const
{
	switch (type) {
	case OptionNone:
		return "none";

	case OptionInteger:
		return "integer";

	case OptionString:
		return "string";

	case OptionKeyValue:
		return "key=value";
	}

	return "unknown";
}

/**
 * \brief Retrieve a string describing the option name, with leading dashes
 * \return A string describing the option name, as a long option identifier
 * (double dash) if the option has a name, or a short option identifier (single
 * dash) otherwise
 */
std::string Option::optionName() const
{
	if (name)
		return "--" + std::string(name);
	else
		return "-" + std::string(1, opt);
}

/* -----------------------------------------------------------------------------
 * OptionBase<T>
 */

/**
 * \class template<typename T> OptionBase
 * \brief Container to store the values of parsed options
 * \tparam T The type through which options are identified
 *
 * The OptionsBase class is generated by a parser (either OptionsParser or
 * KeyValueParser) when parsing options. It stores values for all the options
 * found, and exposes accessor functions to retrieve them. The options are
 * accessed through an identifier to type \a T, which is an int referencing an
 * Option::opt for OptionsParser, or a std::string referencing an Option::name
 * for KeyValueParser.
 */

/**
 * \fn OptionsBase::OptionsBase()
 * \brief Construct an OptionsBase instance
 *
 * The constructed instance is initially invalid, and will be populated by the
 * options parser.
 */

/**
 * \brief Tell if the stored options list is empty
 * \return True if the container is empty, false otherwise
 */
template<typename T>
bool OptionsBase<T>::empty() const
{
	return values_.empty();
}

/**
 * \brief Tell if the options parsing completed successfully
 * \return True if the container is returned after successfully parsing
 * options, false if it is returned after an error was detected during parsing
 */
template<typename T>
bool OptionsBase<T>::valid() const
{
	return valid_;
}

/**
 * \brief Tell if the option \a opt is specified
 * \param[in] opt The option to search for
 * \return True if the \a opt option is set, false otherwise
 */
template<typename T>
bool OptionsBase<T>::isSet(const T &opt) const
{
	return values_.find(opt) != values_.end();
}

/**
 * \brief Retrieve the value of option \a opt
 * \param[in] opt The option to retrieve
 * \return The value of option \a opt if found, an empty OptionValue otherwise
 */
template<typename T>
const OptionValue &OptionsBase<T>::operator[](const T &opt) const
{
	static const OptionValue empty;

	auto it = values_.find(opt);
	if (it != values_.end())
		return it->second;
	return empty;
}

/**
 * \brief Mark the container as invalid
 *
 * This function can be used in a key-value parser's override of the
 * KeyValueParser::parse() function to mark the returned options as invalid if
 * a validation error occurs.
 */
template<typename T>
void OptionsBase<T>::invalidate()
{
	valid_ = false;
}

template<typename T>
bool OptionsBase<T>::parseValue(const T &opt, const Option &option,
				const char *arg)
{
	OptionValue value;

	switch (option.type) {
	case OptionNone:
		break;

	case OptionInteger:
		unsigned int integer;

		if (arg) {
			char *endptr;
			integer = strtoul(arg, &endptr, 0);
			if (*endptr != '\0')
				return false;
		} else {
			integer = 0;
		}

		value = OptionValue(integer);
		break;

	case OptionString:
		value = OptionValue(arg ? arg : "");
		break;

	case OptionKeyValue:
		KeyValueParser *kvParser = option.keyValueParser;
		KeyValueParser::Options keyValues = kvParser->parse(arg);
		if (!keyValues.valid())
			return false;

		value = OptionValue(keyValues);
		break;
	}

	if (option.isArray)
		values_[opt].addValue(value);
	else
		values_[opt] = value;

	return true;
}

template class OptionsBase<int>;
template class OptionsBase<std::string>;

/* -----------------------------------------------------------------------------
 * KeyValueParser
 */

/**
 * \class KeyValueParser
 * \brief A specialized parser for list of key-value pairs
 *
 * The KeyValueParser is an options parser for comma-separated lists of
 * `key=value` pairs. The supported keys are added to the parser with
 * addOption(). A given key can only appear once in the parsed list.
 *
 * Instances of this class can be passed to the OptionsParser::addOption()
 * function to create options that take key-value pairs as an option argument.
 * Specialized versions of the key-value parser can be created by inheriting
 * from this class, to pre-build the options list in the constructor, and to add
 * custom validation by overriding the parse() function.
 */

/**
 * \class KeyValueParser::Options
 * \brief An option list generated by the key-value parser
 *
 * This is a specialization of OptionsBase with the option reference type set to
 * std::string.
 */

KeyValueParser::KeyValueParser() = default;
KeyValueParser::~KeyValueParser() = default;

/**
 * \brief Add a supported option to the parser
 * \param[in] name The option name, corresponding to the key name in the
 * key=value pair. The name shall be unique.
 * \param[in] type The type of the value in the key=value pair
 * \param[in] help The help text
 * \param[in] argument Whether the value is optional, mandatory or not allowed.
 * Shall be ArgumentNone if \a type is OptionNone.
 *
 * \sa OptionsParser
 *
 * \return True if the option was added successfully, false if an error
 * occurred.
 */
bool KeyValueParser::addOption(const char *name, OptionType type,
			       const char *help, OptionArgument argument)
{
	if (!name)
		return false;
	if (!help || help[0] == '\0')
		return false;
	if (argument != ArgumentNone && type == OptionNone)
		return false;

	/* Reject duplicate options. */
	if (optionsMap_.find(name) != optionsMap_.end())
		return false;

	optionsMap_[name] = Option({ 0, type, name, argument, nullptr,
				     help, nullptr, false, nullptr, {} });
	return true;
}

/**
 * \brief Parse a string containing a list of key-value pairs
 * \param[in] arguments The key-value pairs string to parse
 *
 * If a parsing error occurs, the parsing stops and the function returns an
 * invalid container. The container is populated with the options successfully
 * parsed so far.
 *
 * \return A valid container with the list of parsed options on success, or an
 * invalid container otherwise
 */
KeyValueParser::Options KeyValueParser::parse(const char *arguments)
{
	Options options;

	for (const char *pair = arguments; *arguments != '\0'; pair = arguments) {
		const char *comma = strchrnul(arguments, ',');
		size_t len = comma - pair;

		/* Skip over the comma. */
		arguments = *comma == ',' ? comma + 1 : comma;

		/* Skip to the next pair if the pair is empty. */
		if (!len)
			continue;

		std::string key;
		std::string value;

		const char *separator = static_cast<const char *>(memchr(pair, '=', len));
		if (!separator) {
			key = std::string(pair, len);
			value = "";
		} else {
			key = std::string(pair, separator - pair);
			value = std::string(separator + 1, comma - separator - 1);
		}

		/* The key is mandatory, the value might be optional. */
		if (key.empty())
			continue;

		if (optionsMap_.find(key) == optionsMap_.end()) {
			std::cerr << "Invalid option " << key << std::endl;
			return options;
		}

		OptionArgument arg = optionsMap_[key].argument;
		if (value.empty() && arg == ArgumentRequired) {
			std::cerr << "Option " << key << " requires an argument"
				  << std::endl;
			return options;
		} else if (!value.empty() && arg == ArgumentNone) {
			std::cerr << "Option " << key << " takes no argument"
				  << std::endl;
			return options;
		}

		const Option &option = optionsMap_[key];
		if (!options.parseValue(key, option, value.c_str())) {
			std::cerr << "Failed to parse '" << value << "' as "
				  << option.typeName() << " for option " << key
				  << std::endl;
			return options;
		}
	}

	options.valid_ = true;
	return options;
}

unsigned int KeyValueParser::maxOptionLength() const
{
	unsigned int maxLength = 0;

	for (auto const &iter : optionsMap_) {
		const Option &option = iter.second;
		unsigned int length = 10 + strlen(option.name);
		if (option.argument != ArgumentNone)
			length += 1 + strlen(option.typeName());
		if (option.argument == ArgumentOptional)
			length += 2;

		if (length > maxLength)
			maxLength = length;
	}

	return maxLength;
}

void KeyValueParser::usage(int indent)
{
	for (auto const &iter : optionsMap_) {
		const Option &option = iter.second;
		std::string argument = std::string("          ") + option.name;

		if (option.argument != ArgumentNone) {
			if (option.argument == ArgumentOptional)
				argument += "[=";
			else
				argument += "=";
			argument += option.typeName();
			if (option.argument == ArgumentOptional)
				argument += "]";
		}

		std::cerr << std::setw(indent) << argument;

		for (const char *help = option.help, *end = help; end;) {
			end = strchr(help, '\n');
			if (end) {
				std::cerr << std::string(help, end - help + 1);
				std::cerr << std::setw(indent) << " ";
				help = end + 1;
			} else {
				std::cerr << help << std::endl;
			}
		}
	}
}

/* -----------------------------------------------------------------------------
 * OptionValue
 */

/**
 * \class OptionValue
 * \brief Container to store the value of an option
 *
 * The OptionValue class is a variant-type container to store the value of an
 * option. It supports empty values, integers, strings, key-value lists, as well
 * as arrays of those types. For array values, all array elements shall have the
 * same type.
 *
 * OptionValue instances are organized in a tree-based structure that matches
 * the parent-child relationship of the options added to the parser. Children
 * are retrieved with the children() function, and are stored as an
 * OptionsBase<int>.
 */

/**
 * \enum OptionValue::ValueType
 * \brief The option value type
 *
 * \var OptionValue::ValueType::ValueNone
 * \brief Empty value
 *
 * \var OptionValue::ValueType::ValueInteger
 * \brief Integer value (int)
 *
 * \var OptionValue::ValueType::ValueString
 * \brief String value (std::string)
 *
 * \var OptionValue::ValueType::ValueKeyValue
 * \brief Key-value list value (KeyValueParser::Options)
 *
 * \var OptionValue::ValueType::ValueArray
 * \brief Array value
 */

/**
 * \brief Construct an empty OptionValue instance
 *
 * The value type is set to ValueType::ValueNone.
 */
OptionValue::OptionValue()
	: type_(ValueNone), integer_(0)
{
}

/**
 * \brief Construct an integer OptionValue instance
 * \param[in] value The integer value
 *
 * The value type is set to ValueType::ValueInteger.
 */
OptionValue::OptionValue(int value)
	: type_(ValueInteger), integer_(value)
{
}

/**
 * \brief Construct a string OptionValue instance
 * \param[in] value The string value
 *
 * The value type is set to ValueType::ValueString.
 */
OptionValue::OptionValue(const char *value)
	: type_(ValueString), integer_(0), string_(value)
{
}

/**
 * \brief Construct a string OptionValue instance
 * \param[in] value The string value
 *
 * The value type is set to ValueType::ValueString.
 */
OptionValue::OptionValue(const std::string &value)
	: type_(ValueString), integer_(0), string_(value)
{
}

/**
 * \brief Construct a key-value OptionValue instance
 * \param[in] value The key-value list
 *
 * The value type is set to ValueType::ValueKeyValue.
 */
OptionValue::OptionValue(const KeyValueParser::Options &value)
	: type_(ValueKeyValue), integer_(0), keyValues_(value)
{
}

/**
 * \brief Add an entry to an array value
 * \param[in] value The entry value
 *
 * This function can only be called if the OptionValue type is
 * ValueType::ValueNone or ValueType::ValueArray. Upon return, the type will be
 * set to ValueType::ValueArray.
 */
void OptionValue::addValue(const OptionValue &value)
{
	assert(type_ == ValueNone || type_ == ValueArray);

	type_ = ValueArray;
	array_.push_back(value);
}

/**
 * \fn OptionValue::type()
 * \brief Retrieve the value type
 * \return The value type
 */

/**
 * \fn OptionValue::empty()
 * \brief Check if the value is empty
 * \return True if the value is empty (type set to ValueType::ValueNone), or
 * false otherwise
 */

/**
 * \brief Cast the value to an int
 * \return The option value as an int, or 0 if the value type isn't
 * ValueType::ValueInteger
 */
OptionValue::operator int() const
{
	return toInteger();
}

/**
 * \brief Cast the value to a std::string
 * \return The option value as an std::string, or an empty string if the value
 * type isn't ValueType::ValueString
 */
OptionValue::operator std::string() const
{
	return toString();
}

/**
 * \brief Retrieve the value as an int
 * \return The option value as an int, or 0 if the value type isn't
 * ValueType::ValueInteger
 */
int OptionValue::toInteger() const
{
	if (type_ != ValueInteger)
		return 0;

	return integer_;
}

/**
 * \brief Retrieve the value as a std::string
 * \return The option value as a std::string, or an empty string if the value
 * type isn't ValueType::ValueString
 */
std::string OptionValue::toString() const
{
	if (type_ != ValueString)
		return std::string();

	return string_;
}

/**
 * \brief Retrieve the value as a key-value list
 *
 * The behaviour is undefined if the value type isn't ValueType::ValueKeyValue.
 *
 * \return The option value as a KeyValueParser::Options
 */
const KeyValueParser::Options &OptionValue::toKeyValues() const
{
	assert(type_ == ValueKeyValue);
	return keyValues_;
}

/**
 * \brief Retrieve the value as an array
 *
 * The behaviour is undefined if the value type isn't ValueType::ValueArray.
 *
 * \return The option value as a std::vector of OptionValue
 */
const std::vector<OptionValue> &OptionValue::toArray() const
{
	assert(type_ == ValueArray);
	return array_;
}

/**
 * \brief Retrieve the list of child values
 * \return The list of child values
 */
const OptionsParser::Options &OptionValue::children() const
{
	return children_;
}

/* -----------------------------------------------------------------------------
 * OptionsParser
 */

/**
 * \class OptionsParser
 * \brief A command line options parser
 *
 * The OptionsParser class is an easy to use options parser for POSIX-style
 * command line options. Supports short (e.g. `-f`) and long (e.g. `--foo`)
 * options, optional and mandatory arguments, automatic parsing arguments for
 * integer types and comma-separated list of key=value pairs, and multi-value
 * arguments. It handles help text generation automatically.
 *
 * An OptionsParser instance is initialized by adding supported options with
 * addOption(). Options are specified by an identifier and a name. If the
 * identifier is an alphanumeric character, it will be used by the parser as a
 * short option identifier (e.g. `-f`). The name, if specified, will be used as
 * a long option identifier (e.g. `--foo`). It should not include the double
 * dashes. The name is optional if the option identifier is an alphanumeric
 * character and mandatory otherwise.
 *
 * An option has a mandatory help text, which is used to print the full options
 * list with the usage() function. The help text may be a multi-line string.
 * Correct indentation of the help text is handled automatically.
 *
 * Options accept arguments when created with OptionArgument::ArgumentRequired
 * or OptionArgument::ArgumentOptional. If the argument is required, it can be
 * specified as a positional argument after the option (e.g. `-f bar`,
 * `--foo bar`), collated with the short option (e.g. `-fbar`) or separated from
 * the long option by an equal sign (e.g. `--foo=bar`'). When the argument is
 * optional, it must be collated with the short option or separated from the
 * long option by an equal sign.
 *
 * If an option has a required or optional argument, an argument name must be
 * set when adding the option. The argument name is used in the help text as a
 * place holder for an argument value. For instance, a `--write` option that
 * takes a file name as an argument could set the argument name to `filename`,
 * and the help text would display `--write filename`. This is only used to
 * clarify the help text and has no effect on option parsing.
 *
 * The option type tells the parser how to process the argument. Arguments for
 * string options (OptionType::OptionString) are stored as-is without any
 * processing. Arguments for integer options (OptionType::OptionInteger) are
 * converted to an integer value, using an optional base prefix (`0` for base 8,
 * `0x` for base 16, none for base 10). Arguments for key-value options are
 * parsed by a KeyValueParser given to addOption().
 *
 * By default, a given option can appear once only in the parsed command line.
 * If the option is created as an array option, the parser will accept multiple
 * instances of the option. The order in which identical options are specified
 * is preserved in the values of an array option.
 *
 * After preparing the parser, it can be used any number of times to parse
 * command line options with the parse() function. The function returns an
 * Options instance that stores the values for the parsed options. The
 * Options::isSet() function can be used to test if an option has been found,
 * and is the only way to access options that take no argument (specified by
 * OptionType::OptionNone and OptionArgument::ArgumentNone). For options that
 * accept an argument, the option value can be access by Options::operator[]()
 * using the option identifier as the key. The order in which different options
 * are specified on the command line isn't preserved.
 *
 * Options can be created with parent-child relationships to organize them as a
 * tree instead of a flat list. When parsing a command line, the child options
 * are considered related to the parent option that precedes them. This is
 * useful when the parent is an array option. The Options values list generated
 * by the parser then turns into a tree, which each parent value storing the
 * values of child options that follow that instance of the parent option.
 * For instance, with a `capture` option specified as a child of a `camera`
 * array option, parsing the command line
 *
 * `--camera 1 --capture=10 --camera 2 --capture=20`
 *
 * will return an Options instance containing a single OptionValue instance of
 * array type, for the `camera` option. The OptionValue will contain two
 * entries, with the first entry containing the integer value 1 and the second
 * entry the integer value 2. Each of those entries will in turn store an
 * Options instance that contains the respective children. The first entry will
 * store in its children a `capture` option of value 10, and the second entry a
 * `capture` option of value 20.
 *
 * The command line
 *
 * `--capture=10 --camera 1`
 *
 * would result in a parsing error, as the `capture` option has no preceding
 * `camera` option on the command line.
 */

/**
 * \class OptionsParser::Options
 * \brief An option list generated by the options parser
 *
 * This is a specialization of OptionsBase with the option reference type set to
 * int.
 */

OptionsParser::OptionsParser() = default;
OptionsParser::~OptionsParser() = default;

/**
 * \brief Add an option to the parser
 * \param[in] opt The option identifier
 * \param[in] type The type of the option argument
 * \param[in] help The help text (may be a multi-line string)
 * \param[in] name The option name
 * \param[in] argument Whether the option accepts an optional argument, a
 * mandatory argument, or no argument at all
 * \param[in] argumentName The argument name used in the help text
 * \param[in] array Whether the option can appear once or multiple times
 * \param[in] parent The identifier of the parent option (optional)
 *
 * \return True if the option was added successfully, false if an error
 * occurred.
 */
bool OptionsParser::addOption(int opt, OptionType type, const char *help,
			      const char *name, OptionArgument argument,
			      const char *argumentName, bool array, int parent)
{
	/*
	 * Options must have at least a short or long name, and a text message.
	 * If an argument is accepted, it must be described by argumentName.
	 */
	if (!isalnum(opt) && !name)
		return false;
	if (!help || help[0] == '\0')
		return false;
	if (argument != ArgumentNone && !argumentName)
		return false;

	/* Reject duplicate options. */
	if (optionsMap_.find(opt) != optionsMap_.end())
		return false;

	/*
	 * If a parent is specified, create the option as a child of its parent.
	 * Otherwise, create it in the parser's options list.
	 */
	Option *option;

	if (parent) {
		auto iter = optionsMap_.find(parent);
		if (iter == optionsMap_.end())
			return false;

		Option *parentOpt = iter->second;
		parentOpt->children.push_back({
			opt, type, name, argument, argumentName, help, nullptr,
			array, parentOpt, {}
		});
		option = &parentOpt->children.back();
	} else {
		options_.push_back({ opt, type, name, argument, argumentName,
				     help, nullptr, array, nullptr, {} });
		option = &options_.back();
	}

	optionsMap_[opt] = option;

	return true;
}

/**
 * \brief Add a key-value pair option to the parser
 * \param[in] opt The option identifier
 * \param[in] parser The KeyValueParser for the option value
 * \param[in] help The help text (may be a multi-line string)
 * \param[in] name The option name
 * \param[in] array Whether the option can appear once or multiple times
 *
 * \sa Option
 *
 * \return True if the option was added successfully, false if an error
 * occurred.
 */
bool OptionsParser::addOption(int opt, KeyValueParser *parser, const char *help,
			      const char *name, bool array, int parent)
{
	if (!addOption(opt, OptionKeyValue, help, name, ArgumentRequired,
		       "key=value[,key=value,...]", array, parent))
		return false;

	optionsMap_[opt]->keyValueParser = parser;
	return true;
}

/**
 * \brief Parse command line arguments
 * \param[in] argc The number of arguments in the \a argv array
 * \param[in] argv The array of arguments
 *
 * If a parsing error occurs, the parsing stops, the function prints an error
 * message that identifies the invalid argument, prints usage information with
 * usage(), and returns an invalid container. The container is populated with
 * the options successfully parsed so far.
 *
 * \return A valid container with the list of parsed options on success, or an
 * invalid container otherwise
 */
OptionsParser::Options OptionsParser::parse(int argc, char **argv)
{
	OptionsParser::Options options;

	/*
	 * Allocate short and long options arrays large enough to contain all
	 * options.
	 */
	char shortOptions[optionsMap_.size() * 3 + 2];
	struct option longOptions[optionsMap_.size() + 1];
	unsigned int ids = 0;
	unsigned int idl = 0;

	shortOptions[ids++] = ':';

	for (const auto [opt, option] : optionsMap_) {
		if (option->hasShortOption()) {
			shortOptions[ids++] = opt;
			if (option->argument != ArgumentNone)
				shortOptions[ids++] = ':';
			if (option->argument == ArgumentOptional)
				shortOptions[ids++] = ':';
		}

		if (option->hasLongOption()) {
			longOptions[idl].name = option->name;

			switch (option->argument) {
			case ArgumentNone:
				longOptions[idl].has_arg = no_argument;
				break;
			case ArgumentRequired:
				longOptions[idl].has_arg = required_argument;
				break;
			case ArgumentOptional:
				longOptions[idl].has_arg = optional_argument;
				break;
			}

			longOptions[idl].flag = 0;
			longOptions[idl].val = option->opt;
			idl++;
		}
	}

	shortOptions[ids] = '\0';
	memset(&longOptions[idl], 0, sizeof(longOptions[idl]));

	opterr = 0;

	while (true) {
		int c = getopt_long(argc, argv, shortOptions, longOptions, nullptr);

		if (c == -1)
			break;

		if (c == '?' || c == ':') {
			if (c == '?')
				std::cerr << "Invalid option ";
			else
				std::cerr << "Missing argument for option ";
			std::cerr << argv[optind - 1] << std::endl;

			usage();
			return options;
		}

		const Option &option = *optionsMap_[c];
		if (!parseValue(option, optarg, &options)) {
			usage();
			return options;
		}
	}

	if (optind < argc) {
		std::cerr << "Invalid non-option argument '" << argv[optind]
			  << "'" << std::endl;
		usage();
		return options;
	}

	options.valid_ = true;
	return options;
}

/**
 * \brief Print usage text to std::cerr
 *
 * The usage text list all the supported option with their arguments. It is
 * generated automatically from the options added to the parser. Caller of this
 * function may print additional usage information for the application before
 * the list of options.
 */
void OptionsParser::usage()
{
	unsigned int indent = 0;

	for (const auto &opt : optionsMap_) {
		const Option *option = opt.second;
		unsigned int length = 14;
		if (option->hasLongOption())
			length += 2 + strlen(option->name);
		if (option->argument != ArgumentNone)
			length += 1 + strlen(option->argumentName);
		if (option->argument == ArgumentOptional)
			length += 2;
		if (option->isArray)
			length += 4;

		if (length > indent)
			indent = length;

		if (option->keyValueParser) {
			length = option->keyValueParser->maxOptionLength();
			if (length > indent)
				indent = length;
		}
	}

	indent = (indent + 7) / 8 * 8;

	std::cerr << "Options:" << std::endl;

	std::ios_base::fmtflags f(std::cerr.flags());
	std::cerr << std::left;

	usageOptions(options_, indent);

	std::cerr.flags(f);
}

void OptionsParser::usageOptions(const std::list<Option> &options,
				 unsigned int indent)
{
	std::vector<const Option *> parentOptions;

	for (const Option &option : options) {
		std::string argument;
		if (option.hasShortOption())
			argument = std::string("  -")
				 + static_cast<char>(option.opt);
		else
			argument = "    ";

		if (option.hasLongOption()) {
			if (option.hasShortOption())
				argument += ", ";
			else
				argument += "  ";
			argument += std::string("--") + option.name;
		}

		if (option.argument != ArgumentNone) {
			if (option.argument == ArgumentOptional)
				argument += "[=";
			else
				argument += " ";
			argument += option.argumentName;
			if (option.argument == ArgumentOptional)
				argument += "]";
		}

		if (option.isArray)
			argument += " ...";

		std::cerr << std::setw(indent) << argument;

		for (const char *help = option.help, *end = help; end; ) {
			end = strchr(help, '\n');
			if (end) {
				std::cerr << std::string(help, end - help + 1);
				std::cerr << std::setw(indent) << " ";
				help = end + 1;
			} else {
				std::cerr << help << std::endl;
			}
		}

		if (option.keyValueParser)
			option.keyValueParser->usage(indent);

		if (!option.children.empty())
			parentOptions.push_back(&option);
	}

	if (parentOptions.empty())
		return;

	for (const Option *option : parentOptions) {
		std::cerr << std::endl << "Options valid in the context of "
			  << option->optionName() << ":" << std::endl;
		usageOptions(option->children, indent);
	}
}

std::tuple<OptionsParser::Options *, const Option *>
OptionsParser::childOption(const Option *parent, Options *options)
{
	/*
	 * The parent argument points to the parent of the leaf node Option,
	 * and the options argument to the root node of the Options tree. Use
	 * recursive calls to traverse the Option tree up to the root node while
	 * traversing the Options tree down to the leaf node:
	 */

	/*
	 * - If we have no parent, we've reached the root node of the Option
	 *   tree, the options argument is what we need.
	 */
	if (!parent)
		return { options, nullptr };

	/*
	 * - If the parent has a parent, use recursion to move one level up the
	 *   Option tree. This returns the Options corresponding to parent, or
	 *   nullptr if a suitable Options child isn't found.
	 */
	if (parent->parent) {
		const Option *error;
		std::tie(options, error) = childOption(parent->parent, options);

		/* Propagate the error all the way back up the call stack. */
		if (!error)
			return { options, error };
	}

	/*
	 * - The parent has no parent, we're now one level down the root.
	 *   Return the Options child corresponding to the parent. The child may
	 *   not exist if options are specified in an incorrect order.
	 */
	if (!options->isSet(parent->opt))
		return { nullptr, parent };

	/*
	 * If the child value is of array type, children are not stored in the
	 * value .children() list, but in the .children() of the value's array
	 * elements. Use the last array element in that case, as a child option
	 * relates to the last instance of its parent option.
	 */
	const OptionValue *value = &(*options)[parent->opt];
	if (value->type() == OptionValue::ValueArray)
		value = &value->toArray().back();

	return { const_cast<Options *>(&value->children()), nullptr };
}

bool OptionsParser::parseValue(const Option &option, const char *arg,
			       Options *options)
{
	const Option *error;

	std::tie(options, error) = childOption(option.parent, options);
	if (error) {
		std::cerr << "Option " << option.optionName() << " requires a "
			  << error->optionName() << " context" << std::endl;
		return false;
	}

	if (!options->parseValue(option.opt, option, arg)) {
		std::cerr << "Can't parse " << option.typeName()
			  << " argument for option " << option.optionName()
			  << std::endl;
		return false;
	}

	return true;
}
