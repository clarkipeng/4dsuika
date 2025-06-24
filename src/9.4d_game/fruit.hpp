#ifndef FRUIT_MANAGER_HPP
#define FRUIT_MANAGER_HPP

#include <unordered_map>
#include <string>
#include "learnopengl/filesystem.h"
#include "helper.hpp"

enum Fruit {
    CHERRY,
    STRAWBERRY,
    GRAPE,
    DEKOPON,
    PERSIMMON,
    APPLE,
    PEAR,
    PEACH,
    PINEAPPLE,
    MELON,
    WATERMELON,
};

struct FruitProperties {
    float radius;
    float metallic;
    float roughness;
    float ao;
    float alpha;
    unsigned int texture;
};

class FruitManager {
private:
    std::unordered_map<Fruit, FruitProperties> fruitPropertiesMap;
    
    
public:
    FruitManager() {
    }
    
    ~FruitManager() {
        cleanup();
    }
    
    // Initialize all fruit properties and load textures
    void initializeFruits() {
        // Initialize fruit properties with radii from 0.33 to 2.0
        // Each fruit gets progressively larger
        
        fruitPropertiesMap[CHERRY] = {
            0.33f,  // radius
            0.1f,   // metallic
            0.8f,   // roughness
            1.0f,   // ao
            1.0f,   // alpha
            loadTexture(FileSystem::getPath("resources/textures/fruits/cherry.png").c_str())
        };
        
        fruitPropertiesMap[STRAWBERRY] = {
            0.48f,
            0.1f,
            0.7f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/strawberry.png").c_str())
        };
        
        fruitPropertiesMap[GRAPE] = {
            0.63f,
            0.2f,
            0.6f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/grape.png").c_str())
        };
        
        fruitPropertiesMap[DEKOPON] = {
            0.78f,
            0.1f,
            0.5f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/dekopon.png").c_str())
        };
        
        fruitPropertiesMap[PERSIMMON] = {
            0.93f,
            0.1f,
            0.6f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/persimmon.png").c_str())
        };
        
        fruitPropertiesMap[APPLE] = {
            1.08f,
            0.2f,
            0.4f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/apple.png").c_str())
        };
        
        fruitPropertiesMap[PEAR] = {
            1.23f,
            0.1f,
            0.5f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/pear.png").c_str())
        };
        
        fruitPropertiesMap[PEACH] = {
            1.38f,
            0.1f,
            0.3f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/peach.png").c_str())
        };
        
        fruitPropertiesMap[PINEAPPLE] = {
            1.53f,
            0.2f,
            0.7f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/pineapple.png").c_str())
        };
        
        fruitPropertiesMap[MELON] = {
            1.68f,
            0.1f,
            0.4f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/melon.png").c_str())
        };
        
        fruitPropertiesMap[WATERMELON] = {
            2.0f,
            0.1f,
            0.5f,
            1.0f,
            1.0f,
            loadTexture(FileSystem::getPath("resources/textures/fruits/watermelon.png").c_str())
        };
    }
    
    // Get properties for a specific fruit
    const FruitProperties& getFruitProperties(Fruit fruit) const {
        return fruitPropertiesMap.at(fruit);
    }
    
    // Get the next evolution fruit (for merging)
    Fruit getNextFruit(Fruit currentFruit) const {
        switch(currentFruit) {
            case CHERRY: return STRAWBERRY;
            case STRAWBERRY: return GRAPE;
            case GRAPE: return DEKOPON;
            case DEKOPON: return PERSIMMON;
            case PERSIMMON: return APPLE;
            case APPLE: return PEAR;
            case PEAR: return PEACH;
            case PEACH: return PINEAPPLE;
            case PINEAPPLE: return MELON;
            case MELON: return WATERMELON;
            case WATERMELON: return WATERMELON; // Max evolution
            default: return CHERRY;
        }
    }
    
    // Check if fruit can evolve further
    bool canEvolve(Fruit fruit) const {
        return fruit != WATERMELON;
    }
    
    // Merge two fruits of the same type
    Fruit mergeFruits(Fruit fruit1, Fruit fruit2) {
        // Only merge if both fruits are the same type
        if (fruit1 == fruit2 && canEvolve(fruit1)) {
            return getNextFruit(fruit1);
        }
        return CHERRY; // Return original if can't merge
    }
    
    // Get fruit name as string (useful for debugging)
    std::string getFruitName(Fruit fruit) const {
        switch(fruit) {
            case CHERRY: return "Cherry";
            case STRAWBERRY: return "Strawberry";
            case GRAPE: return "Grape";
            case DEKOPON: return "Dekopon";
            case PERSIMMON: return "Persimmon";
            case APPLE: return "Apple";
            case PEAR: return "Pear";
            case PEACH: return "Peach";
            case PINEAPPLE: return "Pineapple";
            case MELON: return "Melon";
            case WATERMELON: return "Watermelon";
            default: return "Unknown";
        }
    }
    
    // Get a random fruit from the first 5 fruits (cherry through persimmon)
    Fruit getRandomFruit() const {
        static const Fruit randomFruits[] = {
            CHERRY,
            STRAWBERRY, 
            GRAPE,
            DEKOPON,
            PERSIMMON
        };
        int randomIndex = rand() % 5;
        return randomFruits[randomIndex];
    }

    // Cleanup textures
    void cleanup() {
        // Delete all loaded textures
        for (auto& pair : fruitPropertiesMap) {
            glDeleteTextures(1, &pair.second.texture);
        }
        fruitPropertiesMap.clear();
    }
} fm;

#endif // FRUIT_MANAGER_HPP