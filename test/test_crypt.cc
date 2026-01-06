// DCF77 Weather Encryption & Region Validation Test
// Tests encryption round-trip AND region/forecast selection
//
// Compile: g++ -std=c++17 -I../include -o test_crypt test_crypt.cc \
//          ../src/dcf77-weather-crypt.cc ../src/dcf77-weather.cc
// Run: ./test_crypt

#include <cstdio>
#include <cstdint>
#include <ctime>
#include "dcf77-weather-crypt.h"
#include "dcf77-weather.h"

//==============================================================================
// Test 1: Encryption Round-Trip
//==============================================================================

bool test_round_trip(uint32_t weather, uint64_t key, const char* desc) {
    printf("  %-35s ", desc);
    
    uint64_t cipher = weather_encode(weather, key);
    int32_t decoded = weather_decode(cipher, key);
    
    if (decoded < 0) {
        printf("FAIL (checksum)\n");
        return false;
    }
    
    if ((uint32_t)decoded != weather) {
        printf("FAIL (0x%06X != 0x%06X)\n", decoded, weather);
        return false;
    }
    
    printf("PASS\n");
    return true;
}

uint64_t make_key(int min, int hour, int day, int month, int weekday, int year) {
    uint64_t key = 0;
    key |= (uint64_t)(min % 10) << 0;
    key |= (uint64_t)(min / 10) << 4;
    key |= (uint64_t)(hour % 10) << 8;
    key |= (uint64_t)(hour / 10) << 12;
    key |= (uint64_t)(day % 10) << 16;
    key |= (uint64_t)(day / 10) << 20;
    key |= (uint64_t)(month % 10) << 24;
    key |= (uint64_t)(month / 10) << 28;
    key |= (uint64_t)(weekday) << 29;
    key |= (uint64_t)(year % 10) << 32;
    key |= (uint64_t)((year / 10) % 10) << 36;
    return key;
}

uint32_t make_weather(int day_code, int night_code, int extra, int temp) {
    uint32_t w = 0;
    w |= (day_code & 0xf);
    w |= (night_code & 0xf) << 4;
    w |= (extra & 0xf) << 8;
    w |= ((temp + 22) & 0x3f) << 16;
    w |= 0x1 << 22;
    return w;
}

int test_encryption() {
    printf("=== Encryption Round-Trip Tests ===\n");
    int passed = 0, failed = 0;
    
    if (test_round_trip(make_weather(1, 1, 0, 20), make_key(0, 12, 6, 1, 1, 26), 
        "Sunny 20C")) passed++; else failed++;
    if (test_round_trip(make_weather(8, 8, 0, 12), make_key(30, 15, 15, 3, 5, 25),
        "Rainy 12C")) passed++; else failed++;
    if (test_round_trip(make_weather(15, 15, 4, -5), make_key(45, 23, 24, 12, 2, 25),
        "Snow storm -5C")) passed++; else failed++;
    if (test_round_trip(make_weather(0, 0, 0, -22), make_key(0, 0, 1, 1, 1, 0),
        "Min values")) passed++; else failed++;
    if (test_round_trip(make_weather(15, 15, 15, 40), make_key(59, 23, 31, 12, 7, 99),
        "Max values")) passed++; else failed++;
    
    printf("  Encryption: %d/%d passed\n\n", passed, passed + failed);
    return failed;
}

//==============================================================================
// Test 2: Region Selection by Time
//==============================================================================

int test_region_selection() {
    printf("=== Region Selection Tests ===\n");
    int passed = 0, failed = 0;
    
    struct TestCase {
        int utc_hour;
        int minute;
        int expected_region;
        const char* desc;
    };
    
    // Region calculation: dataset = ((utc_hour + 2) % 24) * 20 + (minute / 3)
    // Regions 0-59: dataset 0-419 (dataset % 60)
    // Regions 60-89: dataset 420-479 (60 + dataset % 30)
    TestCase tests[] = {
        // 22:00 UTC = dataset 0 = region 0 (Bordeaux)
        {22, 0, 0, "22:00 UTC -> Region 0 (Bordeaux)"},
        {22, 3, 1, "22:03 UTC -> Region 1 (La Rochelle)"},
        {22, 6, 2, "22:06 UTC -> Region 2 (Paris)"},
        
        // Each hour advances 20 datasets
        {23, 0, 20, "23:00 UTC -> Region 20 (Herning)"},
        {0, 0, 40, "00:00 UTC -> Region 40 (Milano)"},
        
        // Key cities at their Type 0 times (Day 1 max, 22:00-00:59 UTC)
        {1, 6, 2, "01:06 UTC -> Region 2 (Paris, type 1)"},
        {1, 36, 12, "01:36 UTC -> Region 12 (Frankfurt, type 1)"},
        {2, 18, 26, "02:18 UTC -> Region 26 (Muenchen, type 1)"},
        // Amsterdam (42) at Type 0: dataset 42 = 22:00 + 2h + 6m = 00:06 UTC
        {0, 6, 42, "00:06 UTC -> Region 42 (Amsterdam)"},
        // Berlin (52) at Type 0: dataset 52 = 22:00 + 2h36m = 00:36 UTC  
        {0, 36, 52, "00:36 UTC -> Region 52 (Berlin)"},
        
        // Dataset 420+ = Regions 60-89
        {19, 0, 60, "19:00 UTC -> Region 60 (Napoli)"},
        {19, 30, 70, "19:30 UTC -> Region 70 (Andorra)"},
        {20, 0, 80, "20:00 UTC -> Region 80 (Sundsvall)"},
    };
    
    for (const auto& t : tests) {
        int region = GetRegionForTime(t.utc_hour, t.minute);
        printf("  %-40s ", t.desc);
        if (region == t.expected_region) {
            printf("PASS\n");
            passed++;
        } else {
            printf("FAIL (got %d)\n", region);
            failed++;
        }
    }
    
    printf("  Region selection: %d/%d passed\n\n", passed, passed + failed);
    return failed;
}

//==============================================================================
// Test 3: Dataset Type (Today/Forecast, Day/Night)
//==============================================================================

int test_dataset_type() {
    printf("=== Dataset Type Tests (Today vs Forecast) ===\n");
    int passed = 0, failed = 0;
    
    // Dataset types 0-7:
    // 0: Day 1 max temp (today, daytime data)
    // 1: Day 1 min temp (today, nighttime data)
    // 2: Day 2 max (tomorrow day)
    // 3: Day 2 min (tomorrow night)
    // 4: Day 3 max
    // 5: Day 3 min
    // 6: Day 4 max
    // 7: Day 4 anomalies + regions 60-89
    
    struct TestCase {
        int utc_hour;
        int minute;
        int expected_type;
        const char* desc;
    };
    
    TestCase tests[] = {
        // Type 0: 22:00-00:59 UTC
        {22, 0, 0, "22:00 UTC -> Type 0 (Day 1 max)"},
        {23, 30, 0, "23:30 UTC -> Type 0 (Day 1 max)"},
        
        // Type 1: 01:00-03:59 UTC  
        {1, 0, 1, "01:00 UTC -> Type 1 (Day 1 min)"},
        {3, 57, 1, "03:57 UTC -> Type 1 (Day 1 min)"},
        
        // Type 2: 04:00-06:59 UTC
        {4, 0, 2, "04:00 UTC -> Type 2 (Day 2 max)"},
        
        // Type 3: 07:00-09:59 UTC
        {7, 0, 3, "07:00 UTC -> Type 3 (Day 2 min)"},
        
        // Type 4: 10:00-12:59 UTC
        {10, 0, 4, "10:00 UTC -> Type 4 (Day 3 max)"},
        
        // Type 5: 13:00-15:59 UTC
        {13, 0, 5, "13:00 UTC -> Type 5 (Day 3 min)"},
        
        // Type 6: 16:00-18:59 UTC
        {16, 0, 6, "16:00 UTC -> Type 6 (Day 4 max)"},
        
        // Type 7: 19:00-21:59 UTC (anomalies + regions 60-89)
        {19, 0, 7, "19:00 UTC -> Type 7 (Day 4 wind/anomalies)"},
        {21, 57, 7, "21:57 UTC -> Type 7 (Day 4 wind/anomalies)"},
    };
    
    for (const auto& t : tests) {
        int dtype = GetDatasetType(t.utc_hour, t.minute);
        printf("  %-40s ", t.desc);
        if (dtype == t.expected_type) {
            printf("PASS\n");
            passed++;
        } else {
            printf("FAIL (got %d)\n", dtype);
            failed++;
        }
    }
    
    printf("  Dataset type: %d/%d passed\n\n", passed, passed + failed);
    return failed;
}

//==============================================================================
// Test 4: Full Weather Generation
//==============================================================================

int test_weather_generation() {
    printf("=== Full Weather Generation Tests ===\n");
    int passed = 0, failed = 0;
    
    RegionWeatherStore store;
    
    // Set weather for Berlin (region 52)
    RegionWeather berlin;
    berlin.weather_day = 1;   // Sunny
    berlin.weather_night = 1; // Clear
    berlin.temperature_day = 25;
    berlin.temperature_night = 18;
    berlin.extreme = 0;
    berlin.rain_probability = 0;
    store.SetRegion(52, berlin);
    store.ApplyPending();
    
    // Set weather for Munich (region 26)
    RegionWeather munich;
    munich.weather_day = 8;   // Light rain
    munich.weather_night = 8;
    munich.temperature_day = 12;
    munich.temperature_night = 8;
    munich.rain_probability = 60;
    store.SetRegion(26, munich);
    store.ApplyPending();
    
    // Test: Get Berlin weather
    const RegionWeather& got_berlin = store.GetRegion(52);
    printf("  Berlin weather_day = %d (expected 1) ", got_berlin.weather_day);
    if (got_berlin.weather_day == 1) { printf("PASS\n"); passed++; } 
    else { printf("FAIL\n"); failed++; }
    
    printf("  Berlin temp_day = %d (expected 25) ", got_berlin.temperature_day);
    if (got_berlin.temperature_day == 25) { printf("PASS\n"); passed++; }
    else { printf("FAIL\n"); failed++; }
    
    // Test: Get Munich weather
    const RegionWeather& got_munich = store.GetRegion(26);
    printf("  Munich weather_day = %d (expected 8) ", got_munich.weather_day);
    if (got_munich.weather_day == 8) { printf("PASS\n"); passed++; }
    else { printf("FAIL\n"); failed++; }
    
    // Test: Unconfigured region returns default
    const RegionWeather& got_paris = store.GetRegion(2);
    printf("  Paris (unconfigured) has default weather_day = %d (expected 1) ", 
           got_paris.weather_day);
    if (got_paris.weather_day == 1) { printf("PASS\n"); passed++; }
    else { printf("FAIL\n"); failed++; }
    
    printf("  Weather generation: %d/%d passed\n\n", passed, passed + failed);
    return failed;
}

//==============================================================================
// Test 5: Weather Packing
//==============================================================================

int test_weather_packing() {
    printf("=== Weather Packing Tests ===\n");
    int passed = 0, failed = 0;
    
    RegionWeather w;
    w.weather_day = 1;
    w.weather_night = 2;
    w.extreme = 5;
    w.rain_probability = 45;
    w.wind_direction = 3;
    w.wind_strength = 6;
    w.temperature_day = 25;
    w.temperature_night = 18;
    
    // Dataset type 0 (even): uses extreme, rain, day temp
    uint32_t packed0 = w.pack(0);
    printf("  Type 0 pack: day=%d ", packed0 & 0xf);
    if ((packed0 & 0xf) == 1) { printf("PASS, "); passed++; } 
    else { printf("FAIL, "); failed++; }
    printf("night=%d ", (packed0 >> 4) & 0xf);
    if (((packed0 >> 4) & 0xf) == 2) { printf("PASS, "); passed++; }
    else { printf("FAIL, "); failed++; }
    printf("extreme=%d ", (packed0 >> 8) & 0xf);
    if (((packed0 >> 8) & 0xf) == 5) { printf("PASS\n"); passed++; }
    else { printf("FAIL\n"); failed++; }
    
    // Dataset type 1 (odd): uses wind_dir, wind_strength, night temp
    uint32_t packed1 = w.pack(1);
    printf("  Type 1 pack: wind_dir=%d ", (packed1 >> 8) & 0xf);
    if (((packed1 >> 8) & 0xf) == 3) { printf("PASS, "); passed++; }
    else { printf("FAIL, "); failed++; }
    printf("wind_str=%d ", (packed1 >> 12) & 0x7);
    // Wind 6 Bft maps to value 3 (see pack function)
    int expected_wind = 3;  // (6+1)/2 = 3 for wind 3-6
    if (((packed1 >> 12) & 0x7) == expected_wind) { printf("PASS\n"); passed++; }
    else { printf("FAIL (got %d, expected %d)\n", (packed1 >> 12) & 0x7, expected_wind); failed++; }
    
    printf("  Weather packing: %d/%d passed\n\n", passed, passed + failed);
    return failed;
}

//==============================================================================
// Main
//==============================================================================

int main() {
    printf("=== DCF77 Weather Validation Suite ===\n\n");
    
    int total_failed = 0;
    total_failed += test_encryption();
    total_failed += test_region_selection();
    total_failed += test_dataset_type();
    total_failed += test_weather_generation();
    total_failed += test_weather_packing();
    
    printf("=== SUMMARY ===\n");
    if (total_failed == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("FAILED: %d test(s)\n", total_failed);
    }
    
    return total_failed > 0 ? 1 : 0;
}
