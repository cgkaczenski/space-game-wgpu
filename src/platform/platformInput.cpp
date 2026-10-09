#include "platformInput.h"

platform::Button keyBoard[platform::Button::BUTTONS_COUNT];
platform::Button leftMouse;
platform::Button rightMouse;

platform::ControllerButtons controllerButtons;
std::string typedInput;
float scrollX = 0.f;
float scrollY = 0.f;

int platform::isButtonHeld(int key)
{
	if (key < Button::A || key >= Button::BUTTONS_COUNT) { return 0; }

	return keyBoard[key].held;
}

int platform::isButtonPressedOn(int key)
{
	if (key < Button::A || key >= Button::BUTTONS_COUNT) { return 0; }

	return keyBoard[key].pressed;
}

int platform::isButtonReleased(int key)
{
	if (key < Button::A || key >= Button::BUTTONS_COUNT) { return 0; }

	return keyBoard[key].released;
}

int platform::isButtonTyped(int key)
{
	if (key < Button::A || key >= Button::BUTTONS_COUNT) { return 0; }

	return keyBoard[key].typed;
}

int platform::isLMousePressed()
{
	return leftMouse.pressed;
}

int platform::isRMousePressed()
{
	return rightMouse.pressed;
}

int platform::isLMouseReleased()
{
	return leftMouse.released;
}

int platform::isRMouseReleased()
{
	return rightMouse.released;
}


int platform::isLMouseHeld()
{
	return leftMouse.held;
}

int platform::isRMouseHeld()
{
	return rightMouse.held;
}

platform::ControllerButtons platform::getControllerButtons()
{
	return platform::isFocused() ? controllerButtons : platform::ControllerButtons{};
}

std::string platform::getTypedInput()
{
	return typedInput;
}

float platform::getScrollY()
{
	return scrollY;
}

float platform::getScrollX()
{
	return scrollX;
}

void platform::internal::addScroll(float x, float y)
{
	scrollX += x;
	scrollY += y;
}

void platform::internal::resetScroll()
{
	scrollX = 0.f;
	scrollY = 0.f;
}

void platform::internal::setButtonState(int button, int newState)
{

	processEventButton(keyBoard[button], newState);

}

void platform::internal::setLeftMouseState(int newState)
{
	processEventButton(leftMouse, newState);

}

void platform::internal::setRightMouseState(int newState)
{
	processEventButton(rightMouse, newState);

}


void platform::internal::updateAllButtons(float deltaTime)
{
	for (int i = 0; i < platform::Button::BUTTONS_COUNT; i++)
	{
		updateButton(keyBoard[i], deltaTime);
	}

	updateButton(leftMouse, deltaTime);
	updateButton(rightMouse, deltaTime);
	
	for(int i=0; i<=GLFW_JOYSTICK_LAST; i++)
	{
		if(glfwJoystickPresent(i) && glfwJoystickIsGamepad(i))
		{
			GLFWgamepadstate state;

			if (glfwGetGamepadState(i, &state))
			{
				for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; b++)
				{
					if(state.buttons[b] == GLFW_PRESS)
					{
						processEventButton(controllerButtons.buttons[b], 1);
					}else
					if (state.buttons[b] == GLFW_RELEASE)
					{
						processEventButton(controllerButtons.buttons[b], 0);
					}
					updateButton(controllerButtons.buttons[b], deltaTime);
				}
				
				controllerButtons.LT = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
				controllerButtons.RT = state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];

				controllerButtons.LStick.x = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
				controllerButtons.LStick.y = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];

				controllerButtons.RStick.x = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
				controllerButtons.RStick.y = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
			
				break;
			}

		}

	}

}

void platform::internal::resetInputsToZero()
{
	resetTypedInput();
	resetScroll();

	for (int i = 0; i < platform::Button::BUTTONS_COUNT; i++)
	{
		resetButtonToZero(keyBoard[i]);
	}

	resetButtonToZero(leftMouse);
	resetButtonToZero(rightMouse);
	
	controllerButtons.setAllToZero();
}

void platform::internal::addToTypedInput(char c)
{
	typedInput += c;
}

void platform::internal::resetTypedInput()
{
	typedInput.clear();
}

const char *platform::buttonName(int key)
{
	static const char *const names[Button::BUTTONS_COUNT] = {
		"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
		"T", "U", "V", "W", "X", "Y", "Z",
		"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
		"SPACE", "ENTER", "ESC", "UP", "DOWN", "LEFT", "RIGHT", "CTRL", "TAB", "-", "=", "SHIFT",
	};
	if (key < Button::A || key >= Button::BUTTONS_COUNT) { return nullptr; }
	return names[key];
}

actions::Source platform::actionSource()
{
	actions::Source s;
	s.keyHeld = [](int k) { return isButtonHeld(k) != 0; };
	s.keyPressed = [](int k) { return isButtonPressedOn(k) != 0; };
	s.keyReleased = [](int k) { return isButtonReleased(k) != 0; };
	s.keyRepeated = [](int k) { return isButtonTyped(k) != 0; };
	s.mouseHeld = [](int b) { return (b == 0 ? isLMouseHeld() : b == 1 ? isRMouseHeld() : 0) != 0; };
	s.mousePressed = [](int b) { return (b == 0 ? isLMousePressed() : b == 1 ? isRMousePressed() : 0) != 0; };
	s.mouseReleased = [](int b) { return (b == 0 ? isLMouseReleased() : b == 1 ? isRMouseReleased() : 0) != 0; };
	s.wheel = [](int axis) { return axis == 0 ? getScrollY() : axis == 1 ? getScrollX() : 0.f; };
	s.keyName = buttonName;
	return s;
}
