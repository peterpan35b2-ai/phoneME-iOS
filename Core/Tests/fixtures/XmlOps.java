package corefixture;

import java.io.ByteArrayInputStream;
import javax.xml.parsers.SAXParser;
import javax.xml.parsers.SAXParserFactory;
import org.xml.sax.Attributes;
import org.xml.sax.helpers.DefaultHandler;

public final class XmlOps {
    private XmlOps() {}

    public static int run() throws Exception {
        final StringBuffer trace = new StringBuffer();
        final int[] attributeStatus = new int[] { 0 };
        SAXParserFactory factory = SAXParserFactory.newInstance();
        factory.setNamespaceAware(false);
        SAXParser parser = factory.newSAXParser();
        String xml = "<?xml version=\"1.0\"?>" +
            "<root a=\"1 &amp; 2\"><item>Xin &lt;chao&gt;</item>" +
            "<![CDATA[+CDATA]]><empty/></root>";
        parser.parse(
            new ByteArrayInputStream(xml.getBytes("UTF-8")),
            new DefaultHandler() {
                public void startElement(String uri, String localName,
                                         String qName,
                                         Attributes attributes) {
                    trace.append('<').append(qName).append('>');
                    if ("root".equals(qName) &&
                        !"1 & 2".equals(attributes.getValue("a"))) {
                        attributeStatus[0] = 1;
                    }
                }

                public void endElement(String uri, String localName,
                                       String qName) {
                    trace.append("</").append(qName).append('>');
                }

                public void characters(char[] characters, int offset,
                                       int length) {
                    trace.append(characters, offset, length);
                }
            });
        if (attributeStatus[0] != 0) return 1;
        return "<root><item>Xin <chao></item>+CDATA<empty></empty></root>"
            .equals(trace.toString()) ? 0 : 2;
    }
}
