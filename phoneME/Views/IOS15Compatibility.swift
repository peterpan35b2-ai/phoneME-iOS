import SwiftUI
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

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

}

struct PhoneMEEmptyStateView: View {
    let title: LocalizedStringKey
    let message: LocalizedStringKey
    let systemImage: String
    var actionTitle: LocalizedStringKey?
    var action: (() -> Void)?

    @ViewBuilder
    var body: some View {
#if os(iOS)
        if #available(iOS 17.0, *) {
            ContentUnavailableView {
                Label(title, systemImage: systemImage)
            } description: {
                Text(message)
            } actions: {
                actionButton
            }
        } else {
            fallbackContent
        }
#else
        fallbackContent
#endif
    }

    private var fallbackContent: some View {
        VStack {
            Image(systemName: systemImage)
                .font(.largeTitle)
                .foregroundStyle(.secondary)
                .accessibilityHidden(true)

            Text(title)
                .font(.headline)

            Text(message)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            actionButton
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder
    private var actionButton: some View {
        if let actionTitle, let action {
            Button(actionTitle, action: action)
                .buttonStyle(.borderedProminent)
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
