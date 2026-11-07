#ifndef ASTRO_UI_INPUT_MODE_H
#define ASTRO_UI_INPUT_MODE_H

#include <QImage>
#include <QObject>

// Notices which kind of input is really in use, as the same app has to suit a touch
// screen in a field and a monitor, keyboard and mouse on a desk.
class InputMode : public QObject {
    Q_OBJECT
    // A key was the last thing used, so show where the keyboard's focus is
    Q_PROPERTY(bool keyboardInUse READ keyboardInUse NOTIFY changed)
    // A real keyboard would make the on-screen one just get in the way. It isn't assumed
    // until a key is pressed, as someone with only a mouse still needs a way to type.
    Q_PROPERTY(bool onScreenKeyboardWanted READ onScreenKeyboardWanted NOTIFY changed)
    // A white pointer would spoil dark adaptation as surely as a white screen
    Q_PROPERTY(bool nightPointer READ nightPointer WRITE setNightPointer NOTIFY changed)

public:
    explicit InputMode(QObject* parent = nullptr);
    ~InputMode() override;

    bool keyboardInUse() const { return m_keyboard_in_use; }
    bool onScreenKeyboardWanted() const { return m_on_screen_keyboard_wanted; }
    bool nightPointer() const { return m_night_pointer; }
    void setNightPointer(bool enabled);

    static QImage nightPointerImage();

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void set_state(bool keyboard_in_use, bool on_screen_keyboard_wanted);

    bool m_keyboard_in_use;
    bool m_on_screen_keyboard_wanted;
    bool m_night_pointer;
};

#endif
