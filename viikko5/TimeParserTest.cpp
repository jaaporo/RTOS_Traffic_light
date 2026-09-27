#include <gtest/gtest.h>
#include "../TimeParser.h"
/* 
3 pisteen tavoite: 

1p: boundary value testit
+1p: tarkistetaan, että merkkijono on tasan 6 merkkiä, tarkistetaan, että palautusarvo ei ole 0 sekuntia
& tarkistetaan, että merkkijonossa on vain numeroita
+1p: lisää testausta liikkennevaloihi:
testataan kelvollinen sekvenssi, tuntematon LED-väri hylätään,
aika 0 hylätään, negatiivinen aika hylätään, kirjaimet ajankohdalla hylätää,
pilkun puuttuminne hylätään, luuan suuri komentomäärä hylätään & yksi komento hyväksytään
          */
// Test suite: TimeParserTest
TEST(TimeParserTest, TestCaseCorrectTime) {

    // Note that this test fails on purpose!!

    // Test with correct time string
    char time_test[] = "000005";
    EXPECT_EQ(time_parse(time_test), 5);

    char time_test2[] = "000105";
    EXPECT_EQ(time_parse(time_test2), 65);

    char time_test3[] = "000120";
    EXPECT_EQ(time_parse(time_test3), 80);
}

TEST(TimeParserTest, TestCaseInCorrectTime) {

    // Test with correct time string
    char time_test[] = "000077";
    EXPECT_EQ(time_parse(time_test), TIME_VALUE_ERROR);

    char time_test2[] = "008800";
    EXPECT_EQ(time_parse(time_test2), TIME_VALUE_ERROR);
}

TEST(TimeParserTest, BoundaryValues){
    // smallest valid value
    char min_time[] = "000001";
    EXPECT_EQ(time_parse(min_time), 1);

    //largest valid value
    char max_time[] = "235959";
    EXPECT_EQ(time_parse(max_time), 59 * 60 + 59);

    //seconds = 60 -> invalid
    char invalid_seconds[] = "000060";
    EXPECT_EQ(time_parse(invalid_seconds), TIME_VALUE_ERROR);

    //minutes = 60 -> invalid
    char invalid_minutes[] = "006000";
    EXPECT_EQ(time_parse(invalid_minutes), TIME_VALUE_ERROR);

    //hours = 24 -> invalid
    char invalid_hours[] = "240000";
    EXPECT_EQ(time_parse(invalid_hours), TIME_VALUE_ERROR);
}


TEST(TimeParserTest, TestStringLen) {
    //correct length -> 6 characters 
    char correct_time[] = "000005";
    EXPECT_EQ(time_parse(correct_time), 5);

    //too short -> 5 characters 
    char too_short[] = "00005";
    EXPECT_EQ(time_parse(too_short), TIME_LEN_ERROR);

    //too long -> 7 characters
    char too_long[] = "0000005";
    EXPECT_EQ(time_parse(too_long), TIME_LEN_ERROR);
}

TEST(TimeParserTest, TestTimeNotZero){
    //correct -> time is greater than zero
    char valid_time[] = "000001";
    EXPECT_EQ(time_parse(valid_time), 1);

    //incorrect -> time is zero
    char zero_time[] = "000000";
    EXPECT_EQ(time_parse(zero_time), TIME_ZERO_ERROR);
}

TEST(TimeParserTest, TestOnlyNumbers){
    //correct -> only numbers
    char valid_time[] = "000105";
    EXPECT_EQ(time_parse(valid_time), 65);

    //incorrect -> contains a letter
    char invalid_time[] = "00A105";
    EXPECT_EQ(time_parse(invalid_time), TIME_CHARACTER_ERROR);
}

TEST(TrafficLightParserTest, ValidSequence){
    char input[] = "R,1000:Y,1000:G,1000";
    TrafficLightCommand commands[10];

    int result = parse_traffic_light_sequence(input, commands, 10);

    EXPECT_EQ(result, 3);

    EXPECT_EQ(commands[0].color, 'R');
    EXPECT_EQ(commands[0].time, 1000);

    EXPECT_EQ(commands[1].color, 'Y');
    EXPECT_EQ(commands[1].time, 1000);

    EXPECT_EQ(commands[2].color, 'G');
    EXPECT_EQ(commands[2].time, 1000);

}

TEST(TrafficLightParserTest, InvalidColor){
    char input[] = "R,1000:B,1000:G,1000";
    TrafficLightCommand commands[10];

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_COLOR_ERROR);
}

TEST(TrafficLightParserTest, InvalidTime){
    char input[] = "R,1000:Y,0:G,1000";
    TrafficLightCommand commands[10];

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_TIME_ERROR);
}

TEST(TrafficLightParserTest, NegativeTime){
    char input[] = "R,-1000";
    TrafficLightCommand commands[10];

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_TIME_ERROR);
}

TEST(TrafficLightParserTest, NonNumericTime){
    char input[] = "R,abc";
    TrafficLightCommand commands[10];

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_TIME_ERROR);
}

TEST(TrafficLightParserTest, MissingComma){
    char input[] = "R1000";
    TrafficLightCommand commands[10];

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_ERROR);
}

TEST(TrafficLightParserTest, TooManyCommands){
    char input[] = "R,100:Y,100:G,100:R,100:Y,100:G,100:R,100:Y,100:G,100:R,100:Y,100";
    TrafficLightCommand commands[10]; 

    EXPECT_EQ(parse_traffic_light_sequence(input, commands, 10), TRAFFIC_LIGHT_ERROR);
}

TEST(TrafficLightParserTest, SingleCommand){
    char input[] = "R,1000";
    TrafficLightCommand commands[10]; 

    int result = parse_traffic_light_sequence(input, commands, 10);

    EXPECT_EQ(result, 1);
    EXPECT_EQ(commands[0].color, 'R');
    EXPECT_EQ(commands[0].time, 1000);
}

// https://google.github.io/googletest/reference/testing.html
// https://google.github.io/googletest/reference/assertions.html
