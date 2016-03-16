<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:folia="http://ilk.uvt.nl/folia"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output method="text" encoding="UTF-8"/>
<xsl:strip-space  elements="*"/>

<xsl:template match="/folia:FoLiA">
  <xsl:apply-templates />
</xsl:template>

<xsl:template match="folia:t">
</xsl:template>


<xsl:template match="folia:morpheme">
  <xsl:if test="./folia:pos/folia:feat[@subset='compound']">
    <xsl:value-of select="folia:t" />
    <xsl:text>&#x9;</xsl:text><xsl:value-of select="folia:pos/folia:feat/@class" />
    <xsl:text>&#x9;</xsl:text><xsl:value-of select="folia:feat[@subset='structure']/@class" />
    <xsl:text>&#xa;</xsl:text>
  </xsl:if>
</xsl:template>

</xsl:stylesheet>
