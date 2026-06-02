#pragma once
#include "Object.h"
#include "RenderManager.h"
#include "InputManager.h"
#include "TextObject.h"
#include <cctype>

class TextField : public Object {
private:
    std::string content = "";
    Vector2 size;
    bool isFocused = false;
    bool prevLeftClick = false;

    TextObject* visualText;
    sf::Color baseColor = sf::Color(50, 50, 50, 255);
    sf::Color focusColor = sf::Color(100, 100, 0, 255);

public:
    inline std::string GetText() const { return content; }

    TextField(Vector2 pos, Vector2 size) : size(size) {
        transform->position = pos;

        visualText = new TextObject("CODIGO...");
        visualText->GetTransform()->position = pos + Vector2(10, 10);
    }

    ~TextField() {
        delete visualText;
    }

    void Update() override {
        float mouseX = (float)Input.GetMouseX();
        float mouseY = (float)Input.GetMouseY();
        bool currentClick = Input.GetLeftClick();

        if (currentClick && !prevLeftClick) {
            isFocused = (mouseX >= transform->position.x && mouseX <= transform->position.x + size.x &&
                mouseY >= transform->position.y && mouseY <= transform->position.y + size.y);

            if (isFocused && content == "") visualText->SetText("");
        }
        prevLeftClick = currentClick;

        if (isFocused) {
            std::string buffer = Input.GetTextBuffer();
            bool textChanged = false;

            for (char c : buffer) {
                if (c == '\b') {
                    if (!content.empty()) {
                        content.pop_back();
                        textChanged = true;
                    }
                }
                else if (c >= 33 && c <= 126 && content.length() < 8) {
                    content += c;
                    textChanged = true;
                }
            }

            if (textChanged) {
                visualText->SetText(content);
            }

            if (content == "") visualText->SetText("...");
        }
        else if (content == "") {
            visualText->SetText("CODIGO...");
        }

        visualText->Update();
    }

    void Render() override {
        RM->DrawRect((short)transform->position.x, (short)transform->position.y,
            (short)size.x, (short)size.y,
            isFocused ? focusColor.r : baseColor.r,
            isFocused ? focusColor.g : baseColor.g,
            isFocused ? focusColor.b : baseColor.b, 255);

        visualText->Render();
    }

    std::string GetContent() { return content; }
};