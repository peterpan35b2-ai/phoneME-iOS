import SwiftUI
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

enum PhoneMEVisualMetrics {
    static let contentMaxWidth: CGFloat = 720
    static let compactContentMaxWidth: CGFloat = 560
    static let horizontalInset: CGFloat = 16
    static let verticalInset: CGFloat = 14
    static let cardCornerRadius: CGFloat = 16
    static let controlCornerRadius: CGFloat = 12
    static let minimumRowHeight: CGFloat = 52
}

extension Color {
    static var phoneMEAppBackground: Color {
#if os(iOS)
        Color(uiColor: .systemGroupedBackground)
#elseif os(macOS)
        Color(nsColor: .windowBackgroundColor)
#else
        Color.gray.opacity(0.08)
#endif
    }

    static var phoneMECardBackground: Color {
#if os(iOS)
        Color(uiColor: .secondarySystemGroupedBackground)
#elseif os(macOS)
        Color(nsColor: .controlBackgroundColor)
#else
        Color.white
#endif
    }

    static var phoneMEControlBackground: Color {
#if os(iOS)
        Color(uiColor: .tertiarySystemGroupedBackground)
#elseif os(macOS)
        Color(nsColor: .textBackgroundColor)
#else
        Color.gray.opacity(0.1)
#endif
    }

    static var phoneMEHairline: Color {
#if os(iOS)
        Color(uiColor: .separator).opacity(0.7)
#elseif os(macOS)
        Color(nsColor: .separatorColor).opacity(0.7)
#else
        Color.primary.opacity(0.12)
#endif
    }
}

struct PhoneMEEmptyStateView: View {
    let title: String
    let message: String
    let systemImage: String
    var actionTitle: String?
    var action: (() -> Void)?

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: systemImage)
                .font(.system(size: 42, weight: .medium))
                .foregroundStyle(.secondary)
                .accessibilityHidden(true)

            VStack(spacing: 5) {
                Text(title)
                    .font(.title3.weight(.semibold))
                Text(message)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if let actionTitle, let action {
                Button(actionTitle, action: action)
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
            }
        }
        .padding(28)
        .frame(maxWidth: 420)
        .background(Color.phoneMECardBackground)
        .clipShape(
            RoundedRectangle(
                cornerRadius: PhoneMEVisualMetrics.cardCornerRadius,
                style: .continuous
            )
        )
        .overlay {
            RoundedRectangle(
                cornerRadius: PhoneMEVisualMetrics.cardCornerRadius,
                style: .continuous
            )
            .stroke(Color.phoneMEHairline, lineWidth: 0.5)
        }
        .padding(PhoneMEVisualMetrics.horizontalInset)
    }
}

struct PhoneMESectionTitle: View {
    let title: String
    var subtitle: String? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title)
                .font(.headline)
            if let subtitle {
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .textCase(nil)
    }
}

struct PhoneMEPressedButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.88 : 1)
            .scaleEffect(configuration.isPressed ? 0.992 : 1)
            .background(
                configuration.isPressed
                    ? Color.accentColor.opacity(0.08)
                    : Color.clear
            )
            .transaction { transaction in
                transaction.animation = nil
            }
    }
}

/// Keeps the iOS 16 navigation behavior while providing a stack-style
/// NavigationView fallback for devices running iOS 15.
struct PhoneMENavigationStack<Content: View>: View {
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    @ViewBuilder
    var body: some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            NavigationStack {
                content
            }
        } else {
            NavigationView {
                content
            }
            .navigationViewStyle(StackNavigationViewStyle())
        }
#else
        NavigationStack {
            content
        }
#endif
    }
}

/// Back-deploys LabeledContent layouts to iOS 15 without changing the
/// higher-level Settings and LCDUI view structure.
struct PhoneMELabeledContent<Label: View, Content: View>: View {
    private let content: Content
    private let label: Label

    init(
        @ViewBuilder content: () -> Content,
        @ViewBuilder label: () -> Label
    ) {
        self.content = content()
        self.label = label()
    }

    @ViewBuilder
    var body: some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            LabeledContent {
                content
            } label: {
                label
            }
        } else {
            HStack(spacing: 12) {
                label
                Spacer(minLength: 12)
                content
            }
        }
#else
        LabeledContent {
            content
        } label: {
            label
        }
#endif
    }
}

extension View {
    /// iOS 16 introduced SwiftUI navigation-bar background control. On iOS 15
    /// the native NavigationView background remains in use.
    @ViewBuilder
    func phoneMENavigationBarBackgroundVisible() -> some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            toolbarBackground(.visible, for: .navigationBar)
        } else {
            self
        }
#else
        self
#endif
    }

    /// Uses the modern toolbar visibility API on iOS 16 and the equivalent
    /// navigationBarHidden fallback on iOS 15.
    @ViewBuilder
    func phoneMENavigationBarVisible(_ isVisible: Bool) -> some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            toolbar(isVisible ? .visible : .hidden, for: .navigationBar)
        } else {
            navigationBarHidden(!isVisible)
        }
#else
        self
#endif
    }

    /// Hides the underlying List/TextEditor scroll background where supported.
    /// iOS 15 keeps its native UIKit-backed background.
    @ViewBuilder
    func phoneMEScrollContentBackgroundHidden() -> some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            scrollContentBackground(.hidden)
        } else {
            self
        }
#else
        self
#endif
    }

    /// Dismisses the keyboard interactively while scrolling where supported.
    /// iOS 15 keeps its native scroll and keyboard behavior.
    @ViewBuilder
    func phoneMEScrollDismissesKeyboardInteractively() -> some View {
#if os(iOS)
        if #available(iOS 16.0, *) {
            scrollDismissesKeyboard(.interactively)
        } else {
            self
        }
#else
        self
#endif
    }
}
