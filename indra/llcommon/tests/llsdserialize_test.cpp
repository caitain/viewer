/** 
 * @file llsdserialize_test.cpp
 * @date 2006-04
 * @brief LLSDSerialize unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */


#include "linden_common.h"

#if LL_WINDOWS
#include <winsock2.h>
typedef U32 uint32_t;
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "llprocess.h"
#include "llstring.h"
#endif

#include "boost/range.hpp"

#include "llsd.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llformat.h"
#include "llmemorystream.h"

#include "../test/lltut.h"
#include "../test/namedtempfile.h"
#include "stringize.h"
#include <cctype>
#include <functional>
#include <iomanip>

typedef std::function<void(const LLSD& data, std::ostream& str)> FormatterFunction;
typedef std::function<bool(std::istream& istr, LLSD& data, llssize max_bytes)> ParserFunction;

std::vector<U8> string_to_vector(const std::string& str)
{
	return std::vector<U8>(str.begin(), str.end());
}

// Format a given byte string as 2-digit hex values, no separators
// Usage: std::cout << hexdump(somestring) << ...
class hexdump
{
public:
    hexdump(const char* data, size_t len):
        hexdump(reinterpret_cast<const U8*>(data), len)
    {}

    hexdump(const U8* data, size_t len):
        mData(data, data + len)
    {}

    hexdump(const hexdump&) = delete;
    hexdump& operator=(const hexdump&) = delete;

    friend std::ostream& operator<<(std::ostream& out, const hexdump& self)
    {
        auto oldfmt{ out.flags() };
        auto oldfill{ out.fill() };
        out.setf(std::ios_base::hex, std::ios_base::basefield);
        out.fill('0');
        for (auto c : self.mData)
        {
            out << std::setw(2) << unsigned(c);
        }
        out.setf(oldfmt, std::ios_base::basefield);
        out.fill(oldfill);
        return out;
    }

private:
    std::vector<U8> mData;
};

// Format a given byte string as a mix of printable characters and, for each
// non-printable character, "\xnn"
// Usage: std::cout << hexmix(somestring) << ...
class hexmix
{
public:
    hexmix(const char* data, size_t len):
        mData(data, len)
    {}

    hexmix(const hexmix&) = delete;
    hexmix& operator=(const hexmix&) = delete;

    friend std::ostream& operator<<(std::ostream& out, const hexmix& self)
    {
        auto oldfmt{ out.flags() };
        auto oldfill{ out.fill() };
        out.setf(std::ios_base::hex, std::ios_base::basefield);
        out.fill('0');
        for (auto c : self.mData)
        {
            // std::isprint() must be passed an unsigned char!
            if (std::isprint(static_cast<unsigned char>(c)))
            {
                out << c;
            }
            else
            {
                out << "\\x" << std::setw(2) << unsigned(c);
            }
        }
        out.setf(oldfmt, std::ios_base::basefield);
        out.fill(oldfill);
        return out;
    }

private:
    std::string mData;
};

namespace tut
{
	struct sd_xml_data
	{
		sd_xml_data()
		{
			mFormatter = new LLSDXMLFormatter;
		}
		LLSD mSD;
		LLPointer<LLSDXMLFormatter> mFormatter;
		void xml_test(const char* name, const std::string& expected)
		{
			std::ostringstream ostr;
			mFormatter->format(mSD, ostr);
			ensure_equals(name, ostr.str(), expected);
		}
	};

	typedef test_group<sd_xml_data> sd_xml_test;
	typedef sd_xml_test::object sd_xml_object;
	tut::sd_xml_test sd_xml_stream("LLSDXMLFormatter");

	template<> template<>
	void sd_xml_object::test<1>()
	{
		// random atomic tests
		std::string expected;

		expected = "<llsd><undef /></llsd>\n";
		xml_test("undef", expected);

		mSD = 3463;
		expected = "<llsd><integer>3463</integer></llsd>\n";
		xml_test("integer", expected);

		mSD = "";
		expected = "<llsd><string /></llsd>\n";
		xml_test("empty string", expected);

		mSD = "foobar";
		expected = "<llsd><string>foobar</string></llsd>\n";
		xml_test("string", expected);

		mSD = LLUUID::null;
		expected = "<llsd><uuid /></llsd>\n";
		xml_test("null uuid", expected);

		mSD = LLUUID("c96f9b1e-f589-4100-9774-d98643ce0bed");
		expected = "<llsd><uuid>c96f9b1e-f589-4100-9774-d98643ce0bed</uuid></llsd>\n";
		xml_test("uuid", expected);

		mSD = LLURI("https://secondlife.com/login");
		expected = "<llsd><uri>https://secondlife.com/login</uri></llsd>\n";
		xml_test("uri", expected);

		mSD = LLDate("2006-04-24T16:11:33Z");
		expected = "<llsd><date>2006-04-24T16:11:33Z</date></llsd>\n";
		xml_test("date", expected);

		// Generated by: echo -n 'hello' | openssl enc -e -base64
		std::vector<U8> hello;
		hello.push_back('h');
		hello.push_back('e');
		hello.push_back('l');
		hello.push_back('l');
		hello.push_back('o');
		mSD = hello;
		expected = "<llsd><binary encoding=\"base64\">aGVsbG8=</binary></llsd>\n";
		xml_test("binary", expected);
	}

	template<> template<>
	void sd_xml_object::test<2>()
	{
		// tests with boolean values.
		std::string expected;

		mFormatter->boolalpha(true);
		mSD = true;
		expected = "<llsd><boolean>true</boolean></llsd>\n";
		xml_test("bool alpha true", expected);
		mSD = false;
		expected = "<llsd><boolean>false</boolean></llsd>\n";
		xml_test("bool alpha false", expected);

		mFormatter->boolalpha(false);
		mSD = true;
		expected = "<llsd><boolean>1</boolean></llsd>\n";
		xml_test("bool true", expected);
		mSD = false;
		expected = "<llsd><boolean>0</boolean></llsd>\n";
		xml_test("bool false", expected);
	}


	template<> template<>
	void sd_xml_object::test<3>()
	{
		// tests with real values.
		std::string expected;

		mFormatter->realFormat("%.2f");
		mSD = 1.0;
		expected = "<llsd><real>1.00</real></llsd>\n";
		xml_test("real 1", expected);

		mSD = -34379.0438;
		expected = "<llsd><real>-34379.04</real></llsd>\n";
		xml_test("real reduced precision", expected);
		mFormatter->realFormat("%.4f");
		expected = "<llsd><real>-34379.0438</real></llsd>\n";
		xml_test("higher precision", expected);

		mFormatter->realFormat("%.0f");
		mSD = 0.0;
		expected = "<llsd><real>0</real></llsd>\n";
		xml_test("no decimal 0", expected);
		mSD = 3287.4387;
		expected = "<llsd><real>3287</real></llsd>\n";
		xml_test("no decimal real number", expected);
	}

	template<> template<>
	void sd_xml_object::test<4>()
	{
		// tests with arrays
		std::string expected;

		mSD = LLSD::emptyArray();
		expected = "<llsd><array /></llsd>\n";
		xml_test("empty array", expected);

		mSD.append(LLSD());
		expected = "<llsd><array><undef /></array></llsd>\n";
		xml_test("1 element array", expected);

		mSD.append(1);
		expected = "<llsd><array><undef /><integer>1</integer></array></llsd>\n";
		xml_test("2 element array", expected);
	}

	template<> template<>
	void sd_xml_object::test<5>()
	{
		// tests with arrays
		std::string expected;

		mSD = LLSD::emptyMap();
		expected = "<llsd><map /></llsd>\n";
		xml_test("empty map", expected);

		mSD["foo"] = "bar";
		expected = "<llsd><map><key>foo</key><string>bar</string></map></llsd>\n";
		xml_test("1 element map", expected);

		mSD["baz"] = LLSD();
		expected = "<llsd><map><key>baz</key><undef /><key>foo</key><string>bar</string></map></llsd>\n";
		xml_test("2 element map", expected);
	}

	template<> template<>
	void sd_xml_object::test<6>()
	{
		// tests with binary
		std::string expected;

		// Generated by: echo -n 'hello' | openssl enc -e -base64
		mSD = string_to_vector("hello");
		expected = "<llsd><binary encoding=\"base64\">aGVsbG8=</binary></llsd>\n";
		xml_test("binary", expected);

		mSD = string_to_vector("6|6|asdfhappybox|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|00000000-0000-0000-0000-000000000000|7fffffff|7fffffff|0|0|82000|450fe394-2904-c9ad-214c-a07eb7feec29|(No Description)|0|10|0");
		expected = "<llsd><binary encoding=\"base64\">Nnw2fGFzZGZoYXBweWJveHw2MGU0NGVjNS0zMDVjLTQzYzItOWExOS1iNGI4OWIxYWUyYTZ8NjBlNDRlYzUtMzA1Yy00M2MyLTlhMTktYjRiODliMWFlMmE2fDYwZTQ0ZWM1LTMwNWMtNDNjMi05YTE5LWI0Yjg5YjFhZTJhNnwwMDAwMDAwMC0wMDAwLTAwMDAtMDAwMC0wMDAwMDAwMDAwMDB8N2ZmZmZmZmZ8N2ZmZmZmZmZ8MHwwfDgyMDAwfDQ1MGZlMzk0LTI5MDQtYzlhZC0yMTRjLWEwN2ViN2ZlZWMyOXwoTm8gRGVzY3JpcHRpb24pfDB8MTB8MA==</binary></llsd>\n";
		xml_test("binary", expected);
	}

	class TestLLSDSerializeData
	{
	public:
		TestLLSDSerializeData();
		~TestLLSDSerializeData();

		void doRoundTripTests(const std::string&);
		void checkRoundTrip(const std::string&, const LLSD& v);

		void setFormatterParser(LLPointer<LLSDFormatter> formatter, LLPointer<LLSDParser> parser)
		{
			mFormatter = [formatter](const LLSD& data, std::ostream& str)
			{
				formatter->format(data, str);
			};
			// this lambda must be mutable since otherwise the bound 'parser'
			// is assumed to point to a const LLSDParser
			mParser = [parser](std::istream& istr, LLSD& data, llssize max_bytes) mutable
			{
				// reset() call is needed since test code re-uses parser object
				parser->reset();
				return (parser->parse(istr, data, max_bytes) > 0);
			};
		}

		void setParser(bool (*parser)(LLSD&, std::istream&, llssize))
		{
			// why does LLSDSerialize::deserialize() reverse the parse() params??
			mParser = [parser](std::istream& istr, LLSD& data, llssize max_bytes)
			{
				return parser(data, istr, max_bytes);
			};
		}

		FormatterFunction mFormatter;
		ParserFunction mParser;
	};

	TestLLSDSerializeData::TestLLSDSerializeData()
	{
	}

	TestLLSDSerializeData::~TestLLSDSerializeData()
	{
	}

	void TestLLSDSerializeData::checkRoundTrip(const std::string& msg, const LLSD& v)
	{
		std::stringstream stream;
		mFormatter(v, stream);
		//LL_INFOS() << "checkRoundTrip: length " << stream.str().length() << LL_ENDL;
		LLSD w;
		mParser(stream, w, stream.str().size());

		try
		{
			ensure_equals(msg, w, v);
		}
		catch (...)
		{
			std::cerr << "the serialized string was:" << std::endl;
			std::cerr << stream.str() << std::endl;
			throw;
		}
	}

	static void fillmap(LLSD& root, U32 width, U32 depth)
	{
		if(depth == 0)
		{
			root["foo"] = "bar";
			return;
		}

		for(U32 i = 0; i < width; ++i)
		{
			std::string key = llformat("child %d", i);
			root[key] = LLSD::emptyMap();
			fillmap(root[key], width, depth - 1);
		}
	}

	void TestLLSDSerializeData::doRoundTripTests(const std::string& msg)
	{
		LLSD v;
		checkRoundTrip(msg + " undefined", v);

		v = true;
		checkRoundTrip(msg + " true bool", v);

		v = false;
		checkRoundTrip(msg + " false bool", v);

		v = 1;
		checkRoundTrip(msg + " positive int", v);

		v = 0;
		checkRoundTrip(msg + " zero int", v);

		v = -1;
		checkRoundTrip(msg + " negative int", v);

		v = 1234.5f;
		checkRoundTrip(msg + " positive float", v);

		v = 0.0f;
		checkRoundTrip(msg + " zero float", v);

		v = -1234.5f;
		checkRoundTrip(msg + " negative float", v);

		// FIXME: need a NaN test

		v = LLUUID::null;
		checkRoundTrip(msg + " null uuid", v);

		LLUUID newUUID;
		newUUID.generate();
		v = newUUID;
		checkRoundTrip(msg + " new uuid", v);

		v = "";
		checkRoundTrip(msg + " empty string", v);

		v = "some string";
		checkRoundTrip(msg + " non-empty string", v);

		v =
"Second Life is a 3-D virtual world entirely built and owned by its residents. "
"Since opening to the public in 2003, it has grown explosively and today is "
"inhabited by nearly 100,000 people from around the globe.\n"
"\n"
"From the moment you enter the World you'll discover a vast digital continent, "
"teeming with people, entertainment, experiences and opportunity. Once you've "
"explored a bit, perhaps you'll find a perfect parcel of land to build your "
"house or business.\n"
"\n"
"You'll also be surrounded by the Creations of your fellow residents. Because "
"residents retain the rights to their digital creations, they can buy, sell "
"and trade with other residents.\n"
"\n"
"The Marketplace currently supports millions of US dollars in monthly "
"transactions. This commerce is handled with the in-world currency, the Linden "
"dollar, which can be converted to US dollars at several thriving online "
"currency exchanges.\n"
"\n"
"Welcome to Second Life. We look forward to seeing you in-world!\n"
		;
		checkRoundTrip(msg + " long string", v);

		static const U32 block_size = 0x000020;
		for (U32 block = 0x000000; block <= 0x10ffff; block += block_size)
		{
			std::ostringstream out;

			for (U32 c = block; c < block + block_size; ++c)
			{
				if (c <= 0x000001f
					&& c != 0x000009
					&& c != 0x00000a)
				{
					// see XML standard, sections 2.2 and 4.1
					continue;
				}
				if (0x00d800 <= c  &&  c <= 0x00dfff) { continue; }
				if (0x00fdd0 <= c  &&  c <= 0x00fdef) { continue; }
				if ((c & 0x00fffe) == 0x00fffe) { continue; }		
					// see Unicode standard, section 15.8 

				if (c <= 0x00007f)
				{
					out << (char)(c & 0x7f);
				}
				else if (c <= 0x0007ff)
				{
					out << (char)(0xc0 | ((c >> 6) & 0x1f));
					out << (char)(0x80 | ((c >> 0) & 0x3f));
				}
				else if (c <= 0x00ffff)
				{
					out << (char)(0xe0 | ((c >> 12) & 0x0f));
					out << (char)(0x80 | ((c >>  6) & 0x3f));
					out << (char)(0x80 | ((c >>  0) & 0x3f));
				}
				else
				{
					out << (char)(0xf0 | ((c >> 18) & 0x07));
					out << (char)(0x80 | ((c >> 12) & 0x3f));
					out << (char)(0x80 | ((c >>  6) & 0x3f));
					out << (char)(0x80 | ((c >>  0) & 0x3f));
				}
			}

			v = out.str();

			std::ostringstream blockmsg;
			blockmsg << msg << " unicode string block 0x" << std::hex << block; 
			checkRoundTrip(blockmsg.str(), v);
		}

		LLDate epoch;
		v = epoch;
		checkRoundTrip(msg + " epoch date", v);

		LLDate aDay("2002-12-07T05:07:15.00Z");
		v = aDay;
		checkRoundTrip(msg + " date", v);

		LLURI path("http://slurl.com/secondlife/Ambleside/57/104/26/");
		v = path;
		checkRoundTrip(msg + " url", v);

		const char source[] = "it must be a blue moon again";
		std::vector<U8> data;
		// note, includes terminating '\0'
		copy(&source[0], &source[sizeof(source)], back_inserter(data));

		v = data;
		checkRoundTrip(msg + " binary", v);

		v = LLSD::emptyMap();
		checkRoundTrip(msg + " empty map", v);

		v = LLSD::emptyMap();
		v["name"] = "luke";		//v.insert("name", "luke");
		v["age"] = 3;			//v.insert("age", 3);
		checkRoundTrip(msg + " map", v);

		v.clear();
		v["a"]["1"] = true;
		v["b"]["0"] = false;
		checkRoundTrip(msg + " nested maps", v);

		v = LLSD::emptyArray();
		checkRoundTrip(msg + " empty array", v);

		v = LLSD::emptyArray();
		v.append("ali");
		v.append(28);
		checkRoundTrip(msg + " array", v);

		v.clear();
		v[0][0] = true;
		v[1][0] = false;
		checkRoundTrip(msg + " nested arrays", v);

		v = LLSD::emptyMap();
		fillmap(v, 10, 3); // 10^6 maps
		checkRoundTrip(msg + " many nested maps", v);
	}

	typedef tut::test_group<TestLLSDSerializeData> TestLLSDSerializeGroup;
	typedef TestLLSDSerializeGroup::object TestLLSDSerializeObject;
	TestLLSDSerializeGroup gTestLLSDSerializeGroup("llsd serialization");

	template<> template<> 
	void TestLLSDSerializeObject::test<1>()
	{
		setFormatterParser(new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_PRETTY_BINARY),
						   new LLSDNotationParser());
		doRoundTripTests("pretty binary notation serialization");
	}

	template<> template<> 
	void TestLLSDSerializeObject::test<2>()
	{
		setFormatterParser(new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_NONE),
						   new LLSDNotationParser());
		doRoundTripTests("raw binary notation serialization");
	}

	template<> template<> 
	void TestLLSDSerializeObject::test<3>()
	{
		setFormatterParser(new LLSDXMLFormatter(), new LLSDXMLParser());
		doRoundTripTests("xml serialization");
	}

	template<> template<> 
	void TestLLSDSerializeObject::test<4>()
	{
		setFormatterParser(new LLSDBinaryFormatter(), new LLSDBinaryParser());
		doRoundTripTests("binary serialization");
	}

	template<> template<>
	void TestLLSDSerializeObject::test<5>()
	{
		mFormatter = [](const LLSD& sd, std::ostream& str)
		{
			LLSDSerialize::serialize(sd, str, LLSDSerialize::LLSD_BINARY);
		};
		setParser(LLSDSerialize::deserialize);
		doRoundTripTests("serialize(LLSD_BINARY)");
	};

	template<> template<>
	void TestLLSDSerializeObject::test<6>()
	{
		mFormatter = [](const LLSD& sd, std::ostream& str)
		{
			LLSDSerialize::serialize(sd, str, LLSDSerialize::LLSD_XML);
		};
		setParser(LLSDSerialize::deserialize);
		doRoundTripTests("serialize(LLSD_XML)");
	};

	template<> template<>
	void TestLLSDSerializeObject::test<7>()
	{
		mFormatter = [](const LLSD& sd, std::ostream& str)
		{
			LLSDSerialize::serialize(sd, str, LLSDSerialize::LLSD_NOTATION);
		};
		setParser(LLSDSerialize::deserialize);
		// In this test, serialize(LLSD_NOTATION) emits a header recognized by
		// deserialize().
		doRoundTripTests("serialize(LLSD_NOTATION)");
	};

	template<> template<>
	void TestLLSDSerializeObject::test<8>()
	{
		setFormatterParser(new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_NONE),
						   new LLSDNotationParser());
		setParser(LLSDSerialize::deserialize);
		// This is an interesting test because LLSDNotationFormatter does not
		// emit an llsd/notation header.
		doRoundTripTests("LLSDNotationFormatter -> deserialize");
	};

	template<> template<>
	void TestLLSDSerializeObject::test<9>()
	{
		setFormatterParser(new LLSDXMLFormatter(false, "", LLSDFormatter::OPTIONS_NONE),
						   new LLSDXMLParser());
		setParser(LLSDSerialize::deserialize);
		// This is an interesting test because LLSDXMLFormatter does not
		// emit an LLSD/XML header.
		doRoundTripTests("LLSDXMLFormatter -> deserialize");
	};

/*==========================================================================*|
	// We do not expect this test to succeed. Without a header, neither
	// notation LLSD nor binary LLSD reliably start with a distinct character,
	// the way XML LLSD starts with '<'. By convention, we default to notation
	// rather than binary.
	template<> template<>
	void TestLLSDSerializeObject::test<10>()
	{
		setFormatterParser(new LLSDBinaryFormatter(false, "", LLSDFormatter::OPTIONS_NONE),
						   new LLSDBinaryParser());
		setParser(LLSDSerialize::deserialize);
		// This is an interesting test because LLSDBinaryFormatter does not
		// emit an LLSD/Binary header.
		doRoundTripTests("LLSDBinaryFormatter -> deserialize");
	};
|*==========================================================================*/

	/**
	 * @class TestLLSDParsing
	 * @brief Base class for of a parse tester.
	 */
	template <class parser_t>
	class TestLLSDParsing
	{
	public:
		TestLLSDParsing()
		{
			mParser = new parser_t;
		}

		void ensureParse(
			const std::string& msg,
			const std::string& in,
			const LLSD& expected_value,
			S32 expected_count,
			S32 depth_limit = -1)
		{
			std::stringstream input;
			input.str(in);

			LLSD parsed_result;
			mParser->reset();	// reset() call is needed since test code re-uses mParser
			S32 parsed_count = mParser->parse(input, parsed_result, in.size(), depth_limit);
			ensure_equals(msg.c_str(), parsed_result, expected_value);

			// This count check is really only useful for expected
			// parse failures, since the ensures equal will already
			// require equality.
			std::string count_msg(msg);
			count_msg += " (count)";
			ensure_equals(count_msg, parsed_count, expected_count);
		}

		LLPointer<parser_t> mParser;
	};


	/**
	 * @class TestLLSDXMLParsing
	 * @brief Concrete instance of a parse tester.
	 */
	class TestLLSDXMLParsing : public TestLLSDParsing<LLSDXMLParser>
	{
	public:
		TestLLSDXMLParsing() {}
	};

	typedef tut::test_group<TestLLSDXMLParsing> TestLLSDXMLParsingGroup;
	typedef TestLLSDXMLParsingGroup::object TestLLSDXMLParsingObject;
	TestLLSDXMLParsingGroup gTestLLSDXMLParsingGroup("llsd XML parsing");

	template<> template<> 
	void TestLLSDXMLParsingObject::test<1>()
	{
		// test handling of xml not recognized as llsd results in an
		// LLSD Undefined
		ensureParse(
			"malformed xml",
			"<llsd><string>ha ha</string>",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"not llsd",
			"<html><body><p>ha ha</p></body></html>",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"value without llsd",
			"<string>ha ha</string>",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"key without llsd",
			"<key>ha ha</key>",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}


	template<> template<> 
	void TestLLSDXMLParsingObject::test<2>()
	{
		// test handling of unrecognized or unparseable llsd values
		LLSD v;
		v["amy"] = 23;
		v["bob"] = LLSD();
		v["cam"] = 1.23;

		ensureParse(
			"unknown data type",
			"<llsd><map>"
				"<key>amy</key><integer>23</integer>"
				"<key>bob</key><bigint>99999999999999999</bigint>"
				"<key>cam</key><real>1.23</real>"
			"</map></llsd>",
			v,
			v.size() + 1);
	}

	template<> template<> 
	void TestLLSDXMLParsingObject::test<3>()
	{
		// test handling of nested bad data

		LLSD v;
		v["amy"] = 23;
		v["cam"] = 1.23;

		ensureParse(
			"map with html",
			"<llsd><map>"
				"<key>amy</key><integer>23</integer>"
				"<html><body>ha ha</body></html>"
				"<key>cam</key><real>1.23</real>"
			"</map></llsd>",
			v,
			v.size() + 1);

		v.clear();
		v["amy"] = 23;
		v["cam"] = 1.23;
		ensureParse(
			"map with value for key",
			"<llsd><map>"
				"<key>amy</key><integer>23</integer>"
				"<string>ha ha</string>"
				"<key>cam</key><real>1.23</real>"
			"</map></llsd>",
			v,
			v.size() + 1);

		v.clear();
		v["amy"] = 23;
		v["bob"] = LLSD::emptyMap();
		v["cam"] = 1.23;
		ensureParse(
			"map with map of html",
			"<llsd><map>"
				"<key>amy</key><integer>23</integer>"
				"<key>bob</key>"
				"<map>"
					"<html><body>ha ha</body></html>"
				"</map>"
				"<key>cam</key><real>1.23</real>"
			"</map></llsd>",
			v,
			v.size() + 1);

		v.clear();
		v[0] = 23;
		v[1] = LLSD();
		v[2] = 1.23;

		ensureParse(
			"array value of html",
			"<llsd><array>"
				"<integer>23</integer>"
				"<html><body>ha ha</body></html>"
				"<real>1.23</real>"
			"</array></llsd>",
			v,
			v.size() + 1);

		v.clear();
		v[0] = 23;
		v[1] = LLSD::emptyMap();
		v[2] = 1.23;
		ensureParse(
			"array with map of html",
			"<llsd><array>"
				"<integer>23</integer>"
				"<map>"
					"<html><body>ha ha</body></html>"
				"</map>"
				"<real>1.23</real>"
			"</array></llsd>",
			v,
			v.size() + 1);
	}

	template<> template<> 
	void TestLLSDXMLParsingObject::test<4>()
	{
		// test handling of binary object in XML
		std::string xml;
		LLSD expected;

		// Generated by: echo -n 'hello' | openssl enc -e -base64
		expected = string_to_vector("hello");
		xml = "<llsd><binary encoding=\"base64\">aGVsbG8=</binary></llsd>\n";
		ensureParse(
			"the word 'hello' packed in binary encoded base64",
			xml,
			expected,
			1);

		expected = string_to_vector("6|6|asdfhappybox|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|00000000-0000-0000-0000-000000000000|7fffffff|7fffffff|0|0|82000|450fe394-2904-c9ad-214c-a07eb7feec29|(No Description)|0|10|0");
		xml = "<llsd><binary encoding=\"base64\">Nnw2fGFzZGZoYXBweWJveHw2MGU0NGVjNS0zMDVjLTQzYzItOWExOS1iNGI4OWIxYWUyYTZ8NjBlNDRlYzUtMzA1Yy00M2MyLTlhMTktYjRiODliMWFlMmE2fDYwZTQ0ZWM1LTMwNWMtNDNjMi05YTE5LWI0Yjg5YjFhZTJhNnwwMDAwMDAwMC0wMDAwLTAwMDAtMDAwMC0wMDAwMDAwMDAwMDB8N2ZmZmZmZmZ8N2ZmZmZmZmZ8MHwwfDgyMDAwfDQ1MGZlMzk0LTI5MDQtYzlhZC0yMTRjLWEwN2ViN2ZlZWMyOXwoTm8gRGVzY3JpcHRpb24pfDB8MTB8MA==</binary></llsd>\n";
		ensureParse(
			"a common binary blob for object -> agent offline inv transfer",
			xml,
			expected,
			1);

		expected = string_to_vector("6|6|asdfhappybox|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|60e44ec5-305c-43c2-9a19-b4b89b1ae2a6|00000000-0000-0000-0000-000000000000|7fffffff|7fffffff|0|0|82000|450fe394-2904-c9ad-214c-a07eb7feec29|(No Description)|0|10|0");
		xml = "<llsd><binary encoding=\"base64\">Nnw2fGFzZGZoYXBweWJveHw2MGU0NGVjNS0zMDVjLTQzYzItOWExOS1iNGI4OWIxYWUyYTZ8NjBl\n";
		xml += "NDRlYzUtMzA1Yy00M2MyLTlhMTktYjRiODliMWFlMmE2fDYwZTQ0ZWM1LTMwNWMtNDNjMi05YTE5\n";
		xml += "LWI0Yjg5YjFhZTJhNnwwMDAwMDAwMC0wMDAwLTAwMDAtMDAwMC0wMDAwMDAwMDAwMDB8N2ZmZmZm\n";
		xml += "ZmZ8N2ZmZmZmZmZ8MHwwfDgyMDAwfDQ1MGZlMzk0LTI5MDQtYzlhZC0yMTRjLWEwN2ViN2ZlZWMy\n";
		xml += "OXwoTm8gRGVzY3JpcHRpb24pfDB8MTB8MA==</binary></llsd>\n";
		ensureParse(
			"a common binary blob for object -> agent offline inv transfer",
			xml,
			expected,
			1);
	}

    template<> template<>
    void TestLLSDXMLParsingObject::test<5>()
    {
        // test deeper nested levels
        LLSD level_5 = LLSD::emptyMap();      level_5["level_5"] = 42.f;
        LLSD level_4 = LLSD::emptyMap();      level_4["level_4"] = level_5;
        LLSD level_3 = LLSD::emptyMap();      level_3["level_3"] = level_4;
        LLSD level_2 = LLSD::emptyMap();      level_2["level_2"] = level_3;
        LLSD level_1 = LLSD::emptyMap();      level_1["level_1"] = level_2;
        LLSD level_0 = LLSD::emptyMap();      level_0["level_0"] = level_1;

        LLSD v;
        v["deep"] = level_0;

        ensureParse(
            "deep llsd xml map",
            "<llsd><map>"
            "<key>deep</key><map>"
            "<key>level_0</key><map>"
            "<key>level_1</key><map>"
            "<key>level_2</key><map>"
            "<key>level_3</key><map>"
            "<key>level_4</key><map>"
            "<key>level_5</key><real>42.0</real>"
            "</map>"
            "</map>"
            "</map>"
            "</map>"
            "</map>"
            "</map>"
            "</map></llsd>",
            v,
            8);
    }


	/*
	TODO:
		test XML parsing
			binary with unrecognized encoding
			nested LLSD tags
			multiple values inside an LLSD
	*/


	/**
	 * @class TestLLSDNotationParsing
	 * @brief Concrete instance of a parse tester.
	 */
	class TestLLSDNotationParsing : public TestLLSDParsing<LLSDNotationParser>
	{
	public:
		TestLLSDNotationParsing() {}
	};

	typedef tut::test_group<TestLLSDNotationParsing> TestLLSDNotationParsingGroup;
	typedef TestLLSDNotationParsingGroup::object TestLLSDNotationParsingObject;
	TestLLSDNotationParsingGroup gTestLLSDNotationParsingGroup(
		"llsd notation parsing");

	template<> template<> 
	void TestLLSDNotationParsingObject::test<1>()
	{
		// test handling of xml not recognized as llsd results in an
		// LLSD Undefined
		ensureParse(
			"malformed notation map",
			"{'ha ha'",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"malformed notation array",
			"['ha ha'",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"malformed notation string",
			"'ha ha",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"bad notation noise",
			"g48ejlnfr",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<2>()
	{
		ensureParse("valid undef", "!", LLSD(), 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<3>()
	{
		LLSD val = false;
		ensureParse("valid boolean false 0", "false", val, 1);
		ensureParse("valid boolean false 1", "f", val, 1);
		ensureParse("valid boolean false 2", "0", val, 1);
		ensureParse("valid boolean false 3", "F", val, 1);
		ensureParse("valid boolean false 4", "FALSE", val, 1);
		val = true;
		ensureParse("valid boolean true 0", "true", val, 1);
		ensureParse("valid boolean true 1", "t", val, 1);
		ensureParse("valid boolean true 2", "1", val, 1);
		ensureParse("valid boolean true 3", "T", val, 1);
		ensureParse("valid boolean true 4", "TRUE", val, 1);

		val.clear();
		ensureParse("invalid true", "TR", val, LLSDParser::PARSE_FAILURE);
		ensureParse("invalid false", "FAL", val, LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<4>()
	{
		LLSD val = 123;
		ensureParse("valid integer", "i123", val, 1);
		val.clear();
		ensureParse("invalid integer", "421", val, LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<5>()
	{
		LLSD val = 456.7;
		ensureParse("valid real", "r456.7", val, 1);
		val.clear();
		ensureParse("invalid real", "456.7", val, LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<6>()
	{
		LLUUID id;
		LLSD val = id;
		ensureParse(
			"unparseable uuid",
			"u123",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		id.generate();
		val = id;
		std::string uuid_str("u");
		uuid_str += id.asString();
		ensureParse("valid uuid", uuid_str.c_str(), val, 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<7>()
	{
		LLSD val = std::string("foolish");
		ensureParse("valid string 1", "\"foolish\"", val, 1);
		val = std::string("g'day");
		ensureParse("valid string 2", "\"g'day\"", val, 1);
		val = std::string("have a \"nice\" day");
		ensureParse("valid string 3", "'have a \"nice\" day'", val, 1);
		val = std::string("whatever");
		ensureParse("valid string 4", "s(8)\"whatever\"", val, 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<8>()
	{
		ensureParse(
			"invalid string 1",
			"s(7)\"whatever\"",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"invalid string 2",
			"s(9)\"whatever\"",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<9>()
	{
		LLSD val = LLURI("http://www.google.com");
		ensureParse("valid uri", "l\"http://www.google.com\"", val, 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<10>()
	{
		LLSD val = LLDate("2007-12-28T09:22:53.10Z");
		ensureParse("valid date", "d\"2007-12-28T09:22:53.10Z\"", val, 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<11>()
	{
		std::vector<U8> vec;
		vec.push_back((U8)'a'); vec.push_back((U8)'b'); vec.push_back((U8)'c');
		vec.push_back((U8)'3'); vec.push_back((U8)'2'); vec.push_back((U8)'1');
		LLSD val = vec;
		ensureParse("valid binary b64", "b64\"YWJjMzIx\"", val, 1);
		ensureParse("valid bainry b16", "b16\"616263333231\"", val, 1);
		ensureParse("valid bainry raw", "b(6)\"abc321\"", val, 1);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<12>()
	{
		ensureParse(
			"invalid -- binary length specified too long",
			"b(7)\"abc321\"",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"invalid -- binary length specified way too long",
			"b(1000000)\"abc321\"",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<13>()
	{
		LLSD val;
		val["amy"] = 23;
		val["bob"] = LLSD();
		val["cam"] = 1.23;
		ensureParse("simple map", "{'amy':i23,'bob':!,'cam':r1.23}", val, 4);

		val["bob"] = LLSD::emptyMap();
		val["bob"]["vehicle"] = std::string("bicycle");
		ensureParse(
			"nested map",
			"{'amy':i23,'bob':{'vehicle':'bicycle'},'cam':r1.23}",
			val,
			5);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<14>()
	{
		LLSD val;
		val.append(23);
		val.append(LLSD());
		val.append(1.23);
		ensureParse("simple array", "[i23,!,r1.23]", val, 4);
		val[1] = LLSD::emptyArray();
		val[1].append("bicycle");
		ensureParse("nested array", "[i23,['bicycle'],r1.23]", val, 5);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<15>()
	{
		LLSD val;
		val["amy"] = 23;
		val["bob"]["dogs"] = LLSD::emptyArray();
		val["bob"]["dogs"].append(LLSD::emptyMap());
		val["bob"]["dogs"][0]["name"] = std::string("groove");
		val["bob"]["dogs"][0]["breed"] = std::string("samoyed");
		val["bob"]["dogs"].append(LLSD::emptyMap());
		val["bob"]["dogs"][1]["name"] = std::string("greyley");
		val["bob"]["dogs"][1]["breed"] = std::string("chow/husky");
		val["cam"] = 1.23;
		ensureParse(
			"nested notation",
			"{'amy':i23,"
			" 'bob':{'dogs':["
			         "{'name':'groove', 'breed':'samoyed'},"
			         "{'name':'greyley', 'breed':'chow/husky'}]},"
			" 'cam':r1.23}",
			val,
			11);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<16>()
	{
		// text to make sure that incorrect sizes bail because 
		std::string bad_str("s(5)\"hi\"");
		ensureParse(
			"size longer than bytes left",
			bad_str,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDNotationParsingObject::test<17>()
	{
		// text to make sure that incorrect sizes bail because 
		std::string bad_bin("b(5)\"hi\"");
		ensureParse(
			"size longer than bytes left",
			bad_bin,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

    template<> template<>
    void TestLLSDNotationParsingObject::test<18>()
    {
        LLSD level_1 = LLSD::emptyMap();		level_1["level_2"] = 99;
        LLSD level_0 = LLSD::emptyMap();		level_0["level_1"] = level_1;

        LLSD deep = LLSD::emptyMap();
        deep["level_0"] = level_0;

        LLSD root = LLSD::emptyMap();
        root["deep"] = deep;

        ensureParse(
            "nested notation 3 deep",
            "{'deep' : {'level_0':{'level_1':{'level_2': i99} } } }",
            root,
            5,
            5); // 4 '{' plus i99 also counts as llsd, so real depth is 5
    }

    template<> template<>
    void TestLLSDNotationParsingObject::test<19>()
    {
        LLSD level_9 = LLSD::emptyMap();      level_9["level_9"] = (S32)99;
        LLSD level_8 = LLSD::emptyMap();      level_8["level_8"] = level_9;
        LLSD level_7 = LLSD::emptyMap();      level_7["level_7"] = level_8;
        LLSD level_6 = LLSD::emptyMap();      level_6["level_6"] = level_7;
        LLSD level_5 = LLSD::emptyMap();      level_5["level_5"] = level_6;
        LLSD level_4 = LLSD::emptyMap();      level_4["level_4"] = level_5;
        LLSD level_3 = LLSD::emptyMap();      level_3["level_3"] = level_4;
        LLSD level_2 = LLSD::emptyMap();      level_2["level_2"] = level_3;
        LLSD level_1 = LLSD::emptyMap();      level_1["level_1"] = level_2;
        LLSD level_0 = LLSD::emptyMap();      level_0["level_0"] = level_1;

        LLSD deep = LLSD::emptyMap();
        deep["deep"] = level_0;

        ensureParse(
            "nested notation 10 deep",
            "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}"
            "} } } } } } } } } }",
            deep,
            12,
            15);
    }

    template<> template<>
    void TestLLSDNotationParsingObject::test<20>()
    {
        LLSD end = LLSD::emptyMap();		  end["end"] = (S32)99;

        LLSD level_49 = LLSD::emptyMap();     level_49["level_49"] = end;
        LLSD level_48 = LLSD::emptyMap();     level_48["level_48"] = level_49;
        LLSD level_47 = LLSD::emptyMap();     level_47["level_47"] = level_48;
        LLSD level_46 = LLSD::emptyMap();     level_46["level_46"] = level_47;
        LLSD level_45 = LLSD::emptyMap();     level_45["level_45"] = level_46;
        LLSD level_44 = LLSD::emptyMap();     level_44["level_44"] = level_45;
        LLSD level_43 = LLSD::emptyMap();     level_43["level_43"] = level_44;
        LLSD level_42 = LLSD::emptyMap();     level_42["level_42"] = level_43;
        LLSD level_41 = LLSD::emptyMap();     level_41["level_41"] = level_42;
        LLSD level_40 = LLSD::emptyMap();     level_40["level_40"] = level_41;

        LLSD level_39 = LLSD::emptyMap();     level_39["level_39"] = level_40;
        LLSD level_38 = LLSD::emptyMap();     level_38["level_38"] = level_39;
        LLSD level_37 = LLSD::emptyMap();     level_37["level_37"] = level_38;
        LLSD level_36 = LLSD::emptyMap();     level_36["level_36"] = level_37;
        LLSD level_35 = LLSD::emptyMap();     level_35["level_35"] = level_36;
        LLSD level_34 = LLSD::emptyMap();     level_34["level_34"] = level_35;
        LLSD level_33 = LLSD::emptyMap();     level_33["level_33"] = level_34;
        LLSD level_32 = LLSD::emptyMap();     level_32["level_32"] = level_33;
        LLSD level_31 = LLSD::emptyMap();     level_31["level_31"] = level_32;
        LLSD level_30 = LLSD::emptyMap();     level_30["level_30"] = level_31;

        LLSD level_29 = LLSD::emptyMap();     level_29["level_29"] = level_30;
        LLSD level_28 = LLSD::emptyMap();     level_28["level_28"] = level_29;
        LLSD level_27 = LLSD::emptyMap();     level_27["level_27"] = level_28;
        LLSD level_26 = LLSD::emptyMap();     level_26["level_26"] = level_27;
        LLSD level_25 = LLSD::emptyMap();     level_25["level_25"] = level_26;
        LLSD level_24 = LLSD::emptyMap();     level_24["level_24"] = level_25;
        LLSD level_23 = LLSD::emptyMap();     level_23["level_23"] = level_24;
        LLSD level_22 = LLSD::emptyMap();     level_22["level_22"] = level_23;
        LLSD level_21 = LLSD::emptyMap();     level_21["level_21"] = level_22;
        LLSD level_20 = LLSD::emptyMap();     level_20["level_20"] = level_21;

        LLSD level_19 = LLSD::emptyMap();     level_19["level_19"] = level_20;
        LLSD level_18 = LLSD::emptyMap();     level_18["level_18"] = level_19;
        LLSD level_17 = LLSD::emptyMap();     level_17["level_17"] = level_18;
        LLSD level_16 = LLSD::emptyMap();     level_16["level_16"] = level_17;
        LLSD level_15 = LLSD::emptyMap();     level_15["level_15"] = level_16;
        LLSD level_14 = LLSD::emptyMap();     level_14["level_14"] = level_15;
        LLSD level_13 = LLSD::emptyMap();     level_13["level_13"] = level_14;
        LLSD level_12 = LLSD::emptyMap();     level_12["level_12"] = level_13;
        LLSD level_11 = LLSD::emptyMap();     level_11["level_11"] = level_12;
        LLSD level_10 = LLSD::emptyMap();     level_10["level_10"] = level_11;

        LLSD level_9 = LLSD::emptyMap();      level_9["level_9"] = level_10;
        LLSD level_8 = LLSD::emptyMap();      level_8["level_8"] = level_9;
        LLSD level_7 = LLSD::emptyMap();      level_7["level_7"] = level_8;
        LLSD level_6 = LLSD::emptyMap();      level_6["level_6"] = level_7;
        LLSD level_5 = LLSD::emptyMap();      level_5["level_5"] = level_6;
        LLSD level_4 = LLSD::emptyMap();      level_4["level_4"] = level_5;
        LLSD level_3 = LLSD::emptyMap();      level_3["level_3"] = level_4;
        LLSD level_2 = LLSD::emptyMap();      level_2["level_2"] = level_3;
        LLSD level_1 = LLSD::emptyMap();      level_1["level_1"] = level_2;
        LLSD level_0 = LLSD::emptyMap();      level_0["level_0"] = level_1;

        LLSD deep = LLSD::emptyMap();
        deep["deep"] = level_0;

        ensureParse(
            "nested notation deep",
            "{'deep':"
            "{'level_0' :{'level_1' :{'level_2' :{'level_3' :{'level_4' :{'level_5' :{'level_6' :{'level_7' :{'level_8' :{'level_9' :"
            "{'level_10':{'level_11':{'level_12':{'level_13':{'level_14':{'level_15':{'level_16':{'level_17':{'level_18':{'level_19':"
            "{'level_20':{'level_21':{'level_22':{'level_23':{'level_24':{'level_25':{'level_26':{'level_27':{'level_28':{'level_29':"
            "{'level_30':{'level_31':{'level_32':{'level_33':{'level_34':{'level_35':{'level_36':{'level_37':{'level_38':{'level_39':"
            "{'level_40':{'level_41':{'level_42':{'level_43':{'level_44':{'level_45':{'level_46':{'level_47':{'level_48':{'level_49':"
            "{'end':i99}"
            "} } } } } } } } } }"
            "} } } } } } } } } }"
            "} } } } } } } } } }"
            "} } } } } } } } } }"
            "} } } } } } } } } }"
            "}",
            deep,
            53);
    }

    template<> template<>
    void TestLLSDNotationParsingObject::test<21>()
    {
        ensureParse(
            "nested notation 10 deep",
            "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}"
            "} } } } } } } } } }",
            LLSD(),
            LLSDParser::PARSE_FAILURE,
            9);
    }

	/**
	 * @class TestLLSDBinaryParsing
	 * @brief Concrete instance of a parse tester.
	 */
	class TestLLSDBinaryParsing : public TestLLSDParsing<LLSDBinaryParser>
	{
	public:
		TestLLSDBinaryParsing() {}
	};

	typedef tut::test_group<TestLLSDBinaryParsing> TestLLSDBinaryParsingGroup;
	typedef TestLLSDBinaryParsingGroup::object TestLLSDBinaryParsingObject;
	TestLLSDBinaryParsingGroup gTestLLSDBinaryParsingGroup(
		"llsd binary parsing");

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<1>()
	{
		std::vector<U8> vec;
		vec.resize(6);
		vec[0] = 'a'; vec[1] = 'b'; vec[2] = 'c';
		vec[3] = '3'; vec[4] = '2'; vec[5] = '1';
		std::string string_expected((char*)&vec[0], vec.size());
		LLSD value = string_expected;

		vec.resize(11);
		vec[0] = 's'; // for string
		vec[5] = 'a'; vec[6] = 'b'; vec[7] = 'c';
		vec[8] = '3'; vec[9] = '2'; vec[10] = '1';

		uint32_t size = htonl(6);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_good((char*)&vec[0], vec.size());
		ensureParse("correct string parse", str_good, value, 1);

		size = htonl(7);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_1((char*)&vec[0], vec.size());
		ensureParse(
			"incorrect size string parse",
			str_bad_1,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		size = htonl(100000);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_2((char*)&vec[0], vec.size());
		ensureParse(
			"incorrect size string parse",
			str_bad_2,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<2>()
	{
		std::vector<U8> vec;
		vec.resize(6);
		vec[0] = 'a'; vec[1] = 'b'; vec[2] = 'c';
		vec[3] = '3'; vec[4] = '2'; vec[5] = '1';
		LLSD value = vec;

		vec.resize(11);
		vec[0] = 'b';  // for binary
		vec[5] = 'a'; vec[6] = 'b'; vec[7] = 'c';
		vec[8] = '3'; vec[9] = '2'; vec[10] = '1';

		uint32_t size = htonl(6);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_good((char*)&vec[0], vec.size());
		ensureParse("correct binary parse", str_good, value, 1);

		size = htonl(7);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_1((char*)&vec[0], vec.size());
		ensureParse(
			"incorrect size binary parse 1",
			str_bad_1,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		size = htonl(100000);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_2((char*)&vec[0], vec.size());
		ensureParse(
			"incorrect size binary parse 2",
			str_bad_2,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<3>()
	{
		// test handling of xml not recognized as llsd results in an
		// LLSD Undefined
		ensureParse(
			"malformed binary map",
			"{'ha ha'",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"malformed binary array",
			"['ha ha'",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"malformed binary string",
			"'ha ha",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
		ensureParse(
			"bad noise",
			"g48ejlnfr",
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}
	template<> template<> 
	void TestLLSDBinaryParsingObject::test<4>()
	{
		ensureParse("valid undef", "!", LLSD(), 1);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<5>()
	{
		LLSD val = false;
		ensureParse("valid boolean false 2", "0", val, 1);
		val = true;
		ensureParse("valid boolean true 2", "1", val, 1);

		val.clear();
		ensureParse("invalid true", "t", val, LLSDParser::PARSE_FAILURE);
		ensureParse("invalid false", "f", val, LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<6>()
	{
		std::vector<U8> vec;
		vec.push_back('{');
		vec.resize(vec.size() + 4);
		uint32_t size = htonl(1);
		memcpy(&vec[1], &size, sizeof(uint32_t));
		vec.push_back('k');
		int key_size_loc = vec.size();
		size = htonl(1); // 1 too short
		vec.resize(vec.size() + 4);
		memcpy(&vec[key_size_loc], &size, sizeof(uint32_t));
		vec.push_back('a'); vec.push_back('m'); vec.push_back('y');
		vec.push_back('i');
		int integer_loc = vec.size();
		vec.resize(vec.size() + 4);
		uint32_t val_int = htonl(23);
		memcpy(&vec[integer_loc], &val_int, sizeof(uint32_t));
		std::string str_bad_1((char*)&vec[0], vec.size());
		ensureParse(
			"invalid key size",
			str_bad_1,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		// check with correct size, but unterminated map (missing '}')
		size = htonl(3); // correct size
		memcpy(&vec[key_size_loc], &size, sizeof(uint32_t));
		std::string str_bad_2((char*)&vec[0], vec.size());
		ensureParse(
			"valid key size, unterminated map",
			str_bad_2,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		// check w/ correct size and correct map termination
		LLSD val;
		val["amy"] = 23;
		vec.push_back('}');
		std::string str_good((char*)&vec[0], vec.size());
		ensureParse(
			"valid map",
			str_good,
			val,
			2);

		// check w/ incorrect sizes and correct map termination
		size = htonl(0); // 1 too few (for the map entry)
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_3((char*)&vec[0], vec.size());
		ensureParse(
			"invalid map too long",
			str_bad_3,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		size = htonl(2); // 1 too many
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_4((char*)&vec[0], vec.size());
		ensureParse(
			"invalid map too short",
			str_bad_4,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<7>()
	{
		std::vector<U8> vec;
		vec.push_back('[');
		vec.resize(vec.size() + 4);
		uint32_t size = htonl(1); // 1 too short
		memcpy(&vec[1], &size, sizeof(uint32_t));
		vec.push_back('"'); vec.push_back('a'); vec.push_back('m');
		vec.push_back('y'); vec.push_back('"'); vec.push_back('i');
		int integer_loc = vec.size();
		vec.resize(vec.size() + 4);
		uint32_t val_int = htonl(23);
		memcpy(&vec[integer_loc], &val_int, sizeof(uint32_t));

		std::string str_bad_1((char*)&vec[0], vec.size());
		ensureParse(
			"invalid array size",
			str_bad_1,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		// check with correct size, but unterminated map (missing ']')
		size = htonl(2); // correct size
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_2((char*)&vec[0], vec.size());
		ensureParse(
			"unterminated array",
			str_bad_2,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		// check w/ correct size and correct map termination
		LLSD val;
		val.append("amy");
		val.append(23);
		vec.push_back(']');
		std::string str_good((char*)&vec[0], vec.size());
		ensureParse(
			"valid array",
			str_good,
			val,
			3);

		// check with too many elements
		size = htonl(3); // 1 too long
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_bad_3((char*)&vec[0], vec.size());
		ensureParse(
			"array too short",
			str_bad_3,
			LLSD(),
			LLSDParser::PARSE_FAILURE);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<8>()
	{
		std::vector<U8> vec;
		vec.push_back('{');
		vec.resize(vec.size() + 4);
		memset(&vec[1], 0, 4);
		vec.push_back('}');
		std::string str_good((char*)&vec[0], vec.size());
		LLSD val = LLSD::emptyMap();
		ensureParse(
			"empty map",
			str_good,
			val,
			1);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<9>()
	{
		std::vector<U8> vec;
		vec.push_back('[');
		vec.resize(vec.size() + 4);
		memset(&vec[1], 0, 4);
		vec.push_back(']');
		std::string str_good((char*)&vec[0], vec.size());
		LLSD val = LLSD::emptyArray();
		ensureParse(
			"empty array",
			str_good,
			val,
			1);
	}

	template<> template<> 
	void TestLLSDBinaryParsingObject::test<10>()
	{
		std::vector<U8> vec;
		vec.push_back('l');
		vec.resize(vec.size() + 4);
		uint32_t size = htonl(14); // 1 too long
		memcpy(&vec[1], &size, sizeof(uint32_t));
		vec.push_back('h'); vec.push_back('t'); vec.push_back('t');
		vec.push_back('p'); vec.push_back(':'); vec.push_back('/');
		vec.push_back('/'); vec.push_back('s'); vec.push_back('l');
		vec.push_back('.'); vec.push_back('c'); vec.push_back('o');
		vec.push_back('m');
		std::string str_bad((char*)&vec[0], vec.size());
		ensureParse(
			"invalid uri length size",
			str_bad,
			LLSD(),
			LLSDParser::PARSE_FAILURE);

		LLSD val;
		val = LLURI("http://sl.com");
		size = htonl(13); // correct length
		memcpy(&vec[1], &size, sizeof(uint32_t));
		std::string str_good((char*)&vec[0], vec.size());
		ensureParse(
			"valid key size",
			str_good,
			val,
			1);
	}

/*
	template<> template<> 
	void TestLLSDBinaryParsingObject::test<11>()
	{
	}
*/

   /**
	 * @class TestLLSDCrossCompatible
	 * @brief Miscellaneous serialization and parsing tests
	 */
	class TestLLSDCrossCompatible
	{
	public:
		TestLLSDCrossCompatible() {}

		void ensureBinaryAndNotation(
			const std::string& msg,
			const LLSD& input)
		{
			// to binary, and back again
			std::stringstream str1;
			S32 count1 = LLSDSerialize::toBinary(input, str1);
			LLSD actual_value_bin;
			S32 count2 = LLSDSerialize::fromBinary(
				actual_value_bin,
				str1,
				LLSDSerialize::SIZE_UNLIMITED);
			ensure_equals(
				"ensureBinaryAndNotation binary count",
				count2,
				count1);

			// to notation and back again
			std::stringstream str2;
			S32 count3 = LLSDSerialize::toNotation(actual_value_bin, str2);
			ensure_equals(
				"ensureBinaryAndNotation notation count1",
				count3,
				count2);
			LLSD actual_value_notation;
			S32 count4 = LLSDSerialize::fromNotation(
				actual_value_notation,
				str2,
				LLSDSerialize::SIZE_UNLIMITED);
			ensure_equals(
				"ensureBinaryAndNotation notation count2",
				count4,
				count3);
			ensure_equals(
				(msg + " (binaryandnotation)").c_str(),
				actual_value_notation,
				input);
		}

		void ensureBinaryAndXML(
			const std::string& msg,
			const LLSD& input)
		{
			// to binary, and back again
			std::stringstream str1;
			S32 count1 = LLSDSerialize::toBinary(input, str1);
			LLSD actual_value_bin;
			S32 count2 = LLSDSerialize::fromBinary(
				actual_value_bin,
				str1,
				LLSDSerialize::SIZE_UNLIMITED);
			ensure_equals(
				"ensureBinaryAndXML binary count",
				count2,
				count1);

			// to xml and back again
			std::stringstream str2;
			S32 count3 = LLSDSerialize::toXML(actual_value_bin, str2);
			ensure_equals(
				"ensureBinaryAndXML xml count1",
				count3,
				count2);
			LLSD actual_value_xml;
			S32 count4 = LLSDSerialize::fromXML(actual_value_xml, str2);
			ensure_equals(
				"ensureBinaryAndXML xml count2",
				count4,
				count3);
			ensure_equals((msg + " (binaryandxml)").c_str(), actual_value_xml, input);
		}
	};

	typedef tut::test_group<TestLLSDCrossCompatible> TestLLSDCompatibleGroup;
	typedef TestLLSDCompatibleGroup::object TestLLSDCompatibleObject;
	TestLLSDCompatibleGroup gTestLLSDCompatibleGroup(
		"llsd serialize compatible");

	template<> template<> 
	void TestLLSDCompatibleObject::test<1>()
	{
		LLSD test;
		ensureBinaryAndNotation("undef", test);
		ensureBinaryAndXML("undef", test);
		test = true;
		ensureBinaryAndNotation("boolean true", test);
		ensureBinaryAndXML("boolean true", test);
		test = false;
		ensureBinaryAndNotation("boolean false", test);
		ensureBinaryAndXML("boolean false", test);
		test = 0;
		ensureBinaryAndNotation("integer zero", test);
		ensureBinaryAndXML("integer zero", test);
		test = 1;
		ensureBinaryAndNotation("integer positive", test);
		ensureBinaryAndXML("integer positive", test);
		test = -234567;
		ensureBinaryAndNotation("integer negative", test);
		ensureBinaryAndXML("integer negative", test);
		test = 0.0;
		ensureBinaryAndNotation("real zero", test);
		ensureBinaryAndXML("real zero", test);
		test = 1.0;
		ensureBinaryAndNotation("real positive", test);
		ensureBinaryAndXML("real positive", test);
		test = -1.0;
		ensureBinaryAndNotation("real negative", test);
		ensureBinaryAndXML("real negative", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<2>()
	{
		LLSD test;
		test = "foobar";
		ensureBinaryAndNotation("string", test);
		ensureBinaryAndXML("string", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<3>()
	{
		LLSD test;
		LLUUID id;
		id.generate();
		test = id;
		ensureBinaryAndNotation("uuid", test);
		ensureBinaryAndXML("uuid", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<4>()
	{
		LLSD test;
		test = LLDate(12345.0);
		ensureBinaryAndNotation("date", test);
		ensureBinaryAndXML("date", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<5>()
	{
		LLSD test;
		test = LLURI("http://www.secondlife.com/");
		ensureBinaryAndNotation("uri", test);
		ensureBinaryAndXML("uri", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<6>()
	{
		LLSD test;
		typedef std::vector<U8> buf_t;
		buf_t val;
		for(int ii = 0; ii < 100; ++ii)
		{
			srand(ii);		/* Flawfinder: ignore */
			S32 size = rand() % 100 + 10;
			std::generate_n(
				std::back_insert_iterator<buf_t>(val),
				size,
				rand);
		}
		test = val;
		ensureBinaryAndNotation("binary", test);
		ensureBinaryAndXML("binary", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<7>()
	{
		LLSD test;
		test = LLSD::emptyArray();
		test.append(1);
		test.append("hello");
		ensureBinaryAndNotation("array", test);
		ensureBinaryAndXML("array", test);
	}

	template<> template<> 
	void TestLLSDCompatibleObject::test<8>()
	{
		LLSD test;
		test = LLSD::emptyArray();
		test["foo"] = "bar";
		test["baz"] = 100;
		ensureBinaryAndNotation("map", test);
		ensureBinaryAndXML("map", test);
	}

    // helper for TestPythonCompatible
    static std::string import_llsd("import os.path\n"
                                   "import sys\n"
                                   "try:\n"
                                   // new freestanding llsd package
                                   "    import llsd\n"
                                   "except ImportError:\n"
                                   // older llbase.llsd module
                                   "    from llbase import llsd\n");

    // helper for TestPythonCompatible
    template <typename CONTENT>
    void python(const std::string& desc, const CONTENT& script, int expect=0)
    {
        auto PYTHON(LLStringUtil::getenv("PYTHON"));
        ensure("Set $PYTHON to the Python interpreter", !PYTHON.empty());

        NamedTempFile scriptfile("py", script);

#if LL_WINDOWS
        std::string q("\"");
        std::string qPYTHON(q + PYTHON + q);
        std::string qscript(q + scriptfile.getName() + q);
        int rc = _spawnl(_P_WAIT, PYTHON.c_str(), qPYTHON.c_str(), qscript.c_str(), NULL);
        if (rc == -1)
        {
            char buffer[256];
            strerror_s(buffer, errno); // C++ can infer the buffer size!  :-O
            ensure(STRINGIZE("Couldn't run Python " << desc << "script: " << buffer), false);
        }
        else
        {
            ensure_equals(STRINGIZE(desc << " script terminated with rc " << rc), rc, expect);
        }

#else  // LL_DARWIN, LL_LINUX
        LLProcess::Params params;
        params.executable = PYTHON;
        params.args.add(scriptfile.getName());
        LLProcessPtr py(LLProcess::create(params));
        ensure(STRINGIZE("Couldn't launch " << desc << " script"), bool(py));
        // Implementing timeout would mean messing with alarm() and
        // catching SIGALRM... later maybe...
        int status(0);
        if (waitpid(py->getProcessID(), &status, 0) == -1)
        {
            int waitpid_errno(errno);
            ensure_equals(STRINGIZE("Couldn't retrieve rc from " << desc << " script: "
                                    "waitpid() errno " << waitpid_errno),
                          waitpid_errno, ECHILD);
        }
        else
        {
            if (WIFEXITED(status))
            {
                int rc(WEXITSTATUS(status));
                ensure_equals(STRINGIZE(desc << " script terminated with rc " << rc),
                              rc, expect);
            }
            else if (WIFSIGNALED(status))
            {
                ensure(STRINGIZE(desc << " script terminated by signal " << WTERMSIG(status)),
                       false);
            }
            else
            {
                ensure(STRINGIZE(desc << " script produced impossible status " << status),
                       false);
            }
        }
#endif
    }

    struct TestPythonCompatible
    {
        TestPythonCompatible() {}
        ~TestPythonCompatible() {}
    };

    typedef tut::test_group<TestPythonCompatible> TestPythonCompatibleGroup;
    typedef TestPythonCompatibleGroup::object TestPythonCompatibleObject;
    TestPythonCompatibleGroup pycompat("LLSD serialize Python compatibility");

    template<> template<>
    void TestPythonCompatibleObject::test<1>()
    {
        set_test_name("verify python()");
        python("hello",
               "import sys\n"
               "sys.exit(17)\n",
               17);                 // expect nonzero rc
    }

    template<> template<>
    void TestPythonCompatibleObject::test<2>()
    {
        set_test_name("verify NamedTempFile");
        python("platform",
               "import sys\n"
               "print('Running on', sys.platform)\n");
    }

    // helper for test<3> - test<7>
    static void writeLLSDArray(const FormatterFunction& serialize,
                               std::ostream& out, const LLSD& array)
    {
        for (const LLSD& item: llsd::inArray(array))
        {
            // It's important to delimit the entries in this file somehow
            // because, although Python's llsd.parse() can accept a file
            // stream, the XML parser expects EOF after a single outer element
            // -- it doesn't just stop. So we must extract a sequence of bytes
            // strings from the file. But since one of the serialization
            // formats we want to test is binary, we can't pick any single
            // byte value as a delimiter! Use a binary integer length prefix
            // instead.
            std::ostringstream buffer;
            serialize(item, buffer);
            auto buffstr{ buffer.str() };
            int bufflen{ static_cast<int>(buffstr.length()) };
            out.write(reinterpret_cast<const char*>(&bufflen), sizeof(bufflen));
            LL_DEBUGS("topy") << "Wrote length: "
                                  << hexdump(reinterpret_cast<const char*>(&bufflen),
                                             sizeof(bufflen))
                                  << LL_ENDL;
            out.write(buffstr.c_str(), buffstr.length());
            LL_DEBUGS("topy") << "Wrote data:   "
                                  << hexmix(buffstr.c_str(), buffstr.length())
                                  << LL_ENDL;
        }
    }

    // helper for test<3> - test<7>
    static void toPythonUsing(const std::string& desc,
                              const FormatterFunction& serialize)
    {
        LLSD cdata(llsd::array(17, 3.14,
                               "This string\n"
                               "has several\n"
                               "lines."));

        const char pydata[] =
            "def verify(iterable):\n"
            "    it = iter(iterable)\n"
            "    assert next(it) == 17\n"
            "    assert abs(next(it) - 3.14) < 0.01\n"
            "    assert next(it) == '''\\\n"
            "This string\n"
            "has several\n"
            "lines.'''\n"
            "    try:\n"
            "        next(it)\n"
            "    except StopIteration:\n"
            "        pass\n"
            "    else:\n"
            "        raise AssertionError('Too many data items')\n";

        // Create an llsdXXXXXX file containing 'data' serialized per
        // FormatterFunction.
        NamedTempFile file("llsd",
                           // NamedTempFile's function constructor
                           // takes a callable. To this callable it passes the
                           // std::ostream with which it's writing the
                           // NamedTempFile.
                           [serialize, cdata]
                           (std::ostream& out)
                           { writeLLSDArray(serialize, out, cdata); });

        python("read C++ " + desc,
               [&](std::ostream& out){ out <<
               import_llsd <<
               "from functools import partial\n"
               "import io\n"
               "import struct\n"
               "lenformat = struct.Struct('i')\n"
               "def parse_each(inf):\n"
               "    for rawlen in iter(partial(inf.read, lenformat.size), b''):\n"
               "        print('Read length:', ''.join(('%02x' % b) for b in rawlen))\n"
               "        len = lenformat.unpack(rawlen)[0]\n"
               // Since llsd.parse() has no max_bytes argument, instead of
               // passing the input stream directly to parse(), read the item
               // into a distinct bytes object and parse that.
               "        data = inf.read(len)\n"
               "        print('Read data:  ', repr(data))\n"
               "        try:\n"
               "            frombytes = llsd.parse(data)\n"
               "        except llsd.LLSDParseError as err:\n"
               "            print(f'*** {err}')\n"
               "            print(f'Bad content:\\n{data!r}')\n"
               "            raise\n"
               // Also try parsing from a distinct stream.
               "        stream = io.BytesIO(data)\n"
               "        fromstream = llsd.parse(stream)\n"
               "        assert frombytes == fromstream\n"
               "        yield frombytes\n"
               << pydata <<
               // Don't forget raw-string syntax for Windows pathnames.
               "verify(parse_each(open(r'" << file.getName() << "', 'rb')))\n";});
    }

    template<> template<>
    void TestPythonCompatibleObject::test<3>()
    {
        set_test_name("to Python using LLSDSerialize::serialize(LLSD_XML)");
        toPythonUsing("LLSD_XML",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_XML); });
    }

    template<> template<>
    void TestPythonCompatibleObject::test<4>()
    {
        set_test_name("to Python using LLSDSerialize::serialize(LLSD_NOTATION)");
        toPythonUsing("LLSD_NOTATION",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_NOTATION); });
    }

    template<> template<>
    void TestPythonCompatibleObject::test<5>()
    {
        set_test_name("to Python using LLSDSerialize::serialize(LLSD_BINARY)");
        toPythonUsing("LLSD_BINARY",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_BINARY); });
    }

    template<> template<>
    void TestPythonCompatibleObject::test<6>()
    {
        set_test_name("to Python using LLSDSerialize::toXML()");
        toPythonUsing("toXML()", LLSDSerialize::toXML);
    }

    template<> template<>
    void TestPythonCompatibleObject::test<7>()
    {
        set_test_name("to Python using LLSDSerialize::toNotation()");
        toPythonUsing("toNotation()", LLSDSerialize::toNotation);
    }

/*==========================================================================*|
    template<> template<>
    void TestPythonCompatibleObject::test<8>()
    {
        set_test_name("to Python using LLSDSerialize::toBinary()");
        // We don't expect this to work because, without a header,
        // llsd.parse() will assume notation rather than binary.
        toPythonUsing("toBinary()", LLSDSerialize::toBinary);
    }
|*==========================================================================*/

    // helper for test<8> - test<12>
    bool itemFromStream(std::istream& istr, LLSD& item, const ParserFunction& parse)
    {
        // reset the output value for debugging clarity
        item.clear();
        // We use an int length prefix as a foolproof delimiter even for
        // binary serialized streams.
        int length{ 0 };
        istr.read(reinterpret_cast<char*>(&length), sizeof(length));
//      return parse(istr, item, length);
        // Sadly, as of 2022-12-01 it seems we can't really trust our LLSD
        // parsers to honor max_bytes: this test works better when we read
        // each item into its own distinct LLMemoryStream, instead of passing
        // the original istr with a max_bytes constraint.
        std::vector<U8> buffer(length);
        istr.read(reinterpret_cast<char*>(buffer.data()), length);
        LLMemoryStream stream(buffer.data(), length);
        return parse(stream, item, length);
    }

    // helper for test<8> - test<12>
    void fromPythonUsing(const std::string& pyformatter,
                         const ParserFunction& parse=
                         [](std::istream& istr, LLSD& data, llssize max_bytes)
                         { return LLSDSerialize::deserialize(data, istr, max_bytes); })
    {
        // Create an empty data file. This is just a placeholder for our
        // script to write into. Create it to establish a unique name that
        // we know.
        NamedTempFile file("llsd", "");

        python("Python " + pyformatter,
               [&](std::ostream& out){ out <<
               import_llsd <<
               "import struct\n"
               "lenformat = struct.Struct('i')\n"
               "DATA = [\n"
               "    17,\n"
               "    3.14,\n"
               "    '''\\\n"
               "This string\n"
               "has several\n"
               "lines.''',\n"
               "]\n"
               // Don't forget raw-string syntax for Windows pathnames.
               // N.B. Using 'print' implicitly adds newlines.
               "with open(r'" << file.getName() << "', 'wb') as f:\n"
               "    for item in DATA:\n"
               "        serialized = llsd." << pyformatter << "(item)\n"
               "        f.write(lenformat.pack(len(serialized)))\n"
               "        f.write(serialized)\n";});

        std::ifstream inf(file.getName().c_str());
        LLSD item;
        try
        {
            ensure("Failed to read LLSD::Integer from Python",
                   itemFromStream(inf, item, parse));
            ensure_equals(item.asInteger(), 17);
            ensure("Failed to read LLSD::Real from Python",
                   itemFromStream(inf, item, parse));
            ensure_approximately_equals("Bad LLSD::Real value from Python",
                                        item.asReal(), 3.14, 7); // 7 bits ~= 0.01
            ensure("Failed to read LLSD::String from Python",
                   itemFromStream(inf, item, parse));
            ensure_equals(item.asString(), 
                          "This string\n"
                          "has several\n"
                          "lines.");
        }
        catch (const tut::failure& err)
        {
            std::cout << "for " << err.what() << ", item = " << item << std::endl;
            throw;
        }
    }

    template<> template<>
    void TestPythonCompatibleObject::test<8>()
    {
        set_test_name("from Python XML using LLSDSerialize::deserialize()");
        fromPythonUsing("format_xml");
    }

    template<> template<>
    void TestPythonCompatibleObject::test<9>()
    {
        set_test_name("from Python notation using LLSDSerialize::deserialize()");
        fromPythonUsing("format_notation");
    }

    template<> template<>
    void TestPythonCompatibleObject::test<10>()
    {
        set_test_name("from Python binary using LLSDSerialize::deserialize()");
        fromPythonUsing("format_binary");
    }

    template<> template<>
    void TestPythonCompatibleObject::test<11>()
    {
        set_test_name("from Python XML using fromXML()");
        // fromXML()'s optional 3rd param isn't max_bytes, it's emit_errors
        fromPythonUsing("format_xml",
                        [](std::istream& istr, LLSD& data, llssize)
                        { return LLSDSerialize::fromXML(data, istr) > 0; });
    }

    template<> template<>
    void TestPythonCompatibleObject::test<12>()
    {
        set_test_name("from Python notation using fromNotation()");
        fromPythonUsing("format_notation",
                        [](std::istream& istr, LLSD& data, llssize max_bytes)
                        { return LLSDSerialize::fromNotation(data, istr, max_bytes) > 0; });
    }

/*==========================================================================*|
    template<> template<>
    void TestPythonCompatibleObject::test<13>()
    {
        set_test_name("from Python binary using fromBinary()");
        // We don't expect this to work because format_binary() emits a
        // header, but fromBinary() won't recognize a header.
        fromPythonUsing("format_binary",
                        [](std::istream& istr, LLSD& data, llssize max_bytes)
                        { return LLSDSerialize::fromBinary(data, istr, max_bytes) > 0; });
    }
|*==========================================================================*/
}
